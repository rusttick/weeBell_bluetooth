// Stage 10 dial and hook tests: decode the rotary dial's digits and report the pulse timing, watch the hook
// contact, and run scored dialing trials.
//
// Wiring (stage 4): pulse contact on IO18 and dial-in-progress (off-normal) contact on IO23, each with the internal
// pull-up and the other side to GND; the hook contact between 3V3 and IO13 / MTCK (R15 is the pull-down).
// The levels the two dial pins have when the dial is at rest are read when `dial on` (or `dial rest`) is typed, so the
// decoder does not need to know the contact polarity. A dial normally has the pulse contact closed at rest and
// opening for each pulse, and the off-normal contact open at rest and closed while the finger wheel is off home.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cmds.h"

#define PIN_HOOK  13
#define PIN_PULSE 18
#define PIN_ONORM 23

#define MAX_PULSES       16
#define FALLBACK_GAP_US  250000   // end a digit after this long without a pulse, if the off-normal contact never moves
#define HOOK_DEBOUNCE_US 30000    // a hook change is accepted and reported once the pad has held its new level this long
#define HOOK_QUIET_US    100000   // a burst of hook edges with no net change is reported after this long
#define POLL_US          500000   // periodic state line
#define DEBOUNCE_US      5000     // a dial contact change is accepted only after the pad has held its new level this long
#define SUSPECT_US       3000     // a pulse break or make shorter than this is probably contact bounce

typedef struct {
    int pin;
    int level;
    int64_t t_us;
} dial_evt_t;

typedef struct {
    int n;                         // breaks counted
    int64_t brk_start[MAX_PULSES];
    int64_t brk_end[MAX_PULSES];   // 0 while the break is still open
    bool overflow;
    int64_t onorm_t;               // when the off-normal contact closed (0 if it never did)
    int64_t last_edge_t;
    bool hook_moved;
} digit_t;

typedef struct {
    int pulse_rest, onorm_rest;
    bool onorm_active;
    bool onorm_seen;               // the off-normal contact has moved at least once since `dial on`
    bool in_digit;
    digit_t d;
    // hook
    int hook_edges, hook_level;
    int64_t hook_first, hook_last;
    int64_t hook_prev_last;        // last edge of the previous report (0 before the first one)
    int64_t hook_gap_min, hook_gap_max;   // shortest and longest time between two edges inside this burst
    // session totals
    int digits, bad_digits, suspect_digits;
    int64_t brk_min, brk_max, make_min, make_max;
    double rate_min, rate_max, ratio_min, ratio_max;
    // trials
    int trials_left, trials_total, target;
    int trial_ok, trial_bad;
    int per_target[10], per_target_ok[10];
    char bad_list[512];
    struct {
        int level;                 // last accepted level
        bool open;                 // a burst of edges is waiting for the pad to settle
        int64_t first_t, last_t;   // first and latest edge of the burst
    } deb[3];                      // 0 = pulse (IO18), 1 = off-normal (IO23), 2 = hook (IO13)
    int glitches, glitches_total;  // dropped edges since the last digit, and in the session
    bool raw;
    bool poll;                     // print the pin states every POLL_US
    int64_t poll_t;
} state_t;

static QueueHandle_t dial_q;
static volatile bool running;
static bool polled_off;   // 'dial poll off' was typed; the state line stays off across 'dial on'
static volatile bool task_alive;
static state_t st;
static int64_t last_raw_t[40];

static void sink(int pin, int level, int64_t t_us)
{
    dial_evt_t e = {.pin = pin, .level = level, .t_us = t_us};
    if (dial_q != NULL) {
        xQueueSend(dial_q, &e, 0);
    }
}

static void new_digit(int64_t t)
{
    memset(&st.d, 0, sizeof(st.d));
    st.d.last_edge_t = t;
}

static void pick_target(void)
{
    st.target = (int)(esp_random() % 10);
    printf("\n>>> trial %d of %d: dial %d\n", st.trials_total - st.trials_left + 1, st.trials_total, st.target);
}

static void trial_score(int got)
{
    if (st.trials_left <= 0) {
        return;
    }
    st.per_target[st.target]++;
    if (got == st.target) {
        st.per_target_ok[st.target]++;
        st.trial_ok++;
    } else {
        st.trial_bad++;
        size_t len = strlen(st.bad_list);
        snprintf(st.bad_list + len, sizeof(st.bad_list) - len, " (wanted %d, got %d)", st.target, got);
    }
    st.trials_left--;
    if (st.trials_left > 0) {
        pick_target();
        return;
    }
    printf("\n=== trials finished: %d right, %d wrong out of %d ===\n", st.trial_ok, st.trial_bad, st.trials_total);
    for (int k = 0; k < 10; k++) {
        if (st.per_target[k] > 0) {
            printf("  digit %d: %d of %d right\n", k, st.per_target_ok[k], st.per_target[k]);
        }
    }
    if (st.trial_bad > 0) {
        printf("  misses:%s\n", st.bad_list);
    }
}

static void finish_digit(int64_t t_end, const char *how)
{
    digit_t *d = &st.d;
    st.in_digit = false;
    if (d->n == 0) {
        printf("off-normal cycle with no pulses (%s), %" PRId64 " ms long\n", how,
               d->onorm_t ? (t_end - d->onorm_t) / 1000 : 0);
        return;
    }

    int64_t brk_sum = 0, make_sum = 0;
    int brk_n = 0, make_n = 0;
    bool suspect = false;
    for (int i = 0; i < d->n; i++) {
        if (d->brk_end[i] == 0) {
            continue;   // never closed again: not a full break
        }
        int64_t b = d->brk_end[i] - d->brk_start[i];
        brk_sum += b;
        brk_n++;
        if (b < SUSPECT_US) {
            suspect = true;
        }
        if (st.brk_min == 0 || b < st.brk_min) st.brk_min = b;
        if (b > st.brk_max) st.brk_max = b;
        if (i + 1 < d->n) {
            int64_t m = d->brk_start[i + 1] - d->brk_end[i];
            make_sum += m;
            make_n++;
            if (m < SUSPECT_US) {
                suspect = true;
            }
            if (st.make_min == 0 || m < st.make_min) st.make_min = m;
            if (m > st.make_max) st.make_max = m;
        }
    }

    int digit = d->n == 10 ? 0 : d->n;
    bool valid = d->n >= 1 && d->n <= 10 && !d->overflow;
    st.digits++;
    if (!valid) {
        st.bad_digits++;
    }
    if (suspect) {
        st.suspect_digits++;
    }

    if (valid) {
        printf("DIGIT %d", digit);
    } else {
        printf("BAD DIGIT (%d%s pulses)", d->n, d->overflow ? "+" : "");
    }
    printf("  pulses %d", d->n);
    double period_us = 0;
    if (d->n >= 2) {
        period_us = (double)(d->brk_start[d->n - 1] - d->brk_start[0]) / (d->n - 1);
        double rate = 1e6 / period_us;
        printf("  rate %.2f/s", rate);
        if (st.digits == 1 || rate < st.rate_min) st.rate_min = rate;
        if (st.digits == 1 || rate > st.rate_max) st.rate_max = rate;
    }
    if (brk_n > 0) {
        printf("  break %.1f ms", (double)brk_sum / brk_n / 1000.0);
    }
    if (make_n > 0) {
        printf("  make %.1f ms", (double)make_sum / make_n / 1000.0);
    }
    if (brk_n > 0 && make_n > 0) {
        double brk_mean = (double)brk_sum / brk_n, make_mean = (double)make_sum / make_n;
        double ratio = 100.0 * brk_mean / (brk_mean + make_mean);
        printf("  break %.0f%%", ratio);
        if (st.digits == 1 || ratio < st.ratio_min) st.ratio_min = ratio;
        if (st.digits == 1 || ratio > st.ratio_max) st.ratio_max = ratio;
    }
    if (d->onorm_t != 0) {
        printf("  lead %" PRId64 " ms  tail %" PRId64 " ms", (d->brk_start[0] - d->onorm_t) / 1000,
               d->brk_end[d->n - 1] ? (t_end - d->brk_end[d->n - 1]) / 1000 : -1);
    }
    printf("  [%s]", how);
    if (st.glitches > 0) {
        printf("  (%d chatter edge%s ignored)", st.glitches, st.glitches == 1 ? "" : "s");
        st.glitches = 0;
    }
    if (suspect) {
        printf("  SUSPECT: a break or make under %d ms (bounce?)", SUSPECT_US / 1000);
    }
    if (d->hook_moved) {
        printf("  HOOK MOVED DURING DIGIT");
    }
    printf("\n");

    if (st.trials_left > 0) {
        trial_score(valid ? digit : -1);
    }
}

static void on_pulse(int level, int64_t t)
{
    digit_t *d = &st.d;
    bool brk = level != st.pulse_rest;
    if (!st.in_digit) {
        // With no off-normal contact ever seen, the first break starts a digit and a gap ends it.
        if (brk && !st.onorm_seen) {
            st.in_digit = true;
            new_digit(t);
        } else if (brk) {
            printf("stray pulse break outside the off-normal contact (off-normal is %s)\n",
                   st.onorm_active ? "active?" : "at rest");
            return;
        } else {
            return;
        }
    }
    d->last_edge_t = t;
    if (brk) {
        if (d->n < MAX_PULSES) {
            d->brk_start[d->n] = t;
            d->brk_end[d->n] = 0;
            d->n++;
        } else {
            d->overflow = true;
        }
    } else if (d->n > 0 && d->n <= MAX_PULSES && d->brk_end[d->n - 1] == 0) {
        d->brk_end[d->n - 1] = t;
    }
}

static void on_onorm(int level, int64_t t)
{
    bool active = level != st.onorm_rest;
    st.onorm_seen = true;
    st.onorm_active = active;
    if (active) {
        if (st.in_digit) {
            finish_digit(t, "restarted");
        }
        st.in_digit = true;
        new_digit(t);
        st.d.onorm_t = t;
    } else if (st.in_digit) {
        finish_digit(t, "off-normal");
    }
}

static void on_hook(int level, int64_t t)
{
    if (st.hook_edges == 0) {
        st.hook_first = t;
        st.hook_gap_min = INT64_MAX;
        st.hook_gap_max = 0;
    } else {
        int64_t gap = t - st.hook_last;
        if (gap < st.hook_gap_min) st.hook_gap_min = gap;
        if (gap > st.hook_gap_max) st.hook_gap_max = gap;
    }
    st.hook_edges++;
    st.hook_level = level;
    st.hook_last = t;
    if (st.in_digit) {
        st.d.hook_moved = true;
    }
}

// Report an accepted (debounced) hook change. The edge counts show how much the contact chattered before it settled.
static void hook_report(int level)
{
    printf("HOOK -> %d (%s), debounced over %d ms, %d edge%s over %" PRId64 " us%s\n", level,
           level ? "contact closed, 3V3 to MTCK" : "contact open", HOOK_DEBOUNCE_US / 1000, st.hook_edges,
           st.hook_edges == 1 ? "" : "s", st.hook_last - st.hook_first, st.hook_edges > 1 ? "  (BOUNCE)" : "");
    if (st.hook_prev_last != 0) {
        printf("  held the previous state %" PRId64 " ms before this change\n",
               (st.hook_first - st.hook_prev_last) / 1000);
    }
    if (st.hook_edges > 1) {
        printf("  gaps between edges: shortest %" PRId64 " us, longest %" PRId64 " us\n", st.hook_gap_min,
               st.hook_gap_max);
    }
    st.hook_prev_last = st.hook_last;
    st.hook_edges = 0;
}

static void tick(int64_t now)
{
    if (st.poll && now - st.poll_t >= POLL_US) {
        st.poll_t = now;
        int64_t hook_edge = st.hook_edges > 0 ? st.hook_last : st.hook_prev_last;
        printf("state: hook %d, pulse %d, off-normal %d", gpio_get_level(PIN_HOOK), gpio_get_level(PIN_PULSE),
               gpio_get_level(PIN_ONORM));
        if (hook_edge != 0) {
            printf("  (hook last changed %" PRId64 " ms ago)", (now - hook_edge) / 1000);
        }
        printf("\n");
    }
    if (st.in_digit && !st.onorm_seen && st.d.n > 0 && now - st.d.last_edge_t > FALLBACK_GAP_US) {
        finish_digit(st.d.last_edge_t, "gap timeout, no off-normal contact seen");
    }
    if (st.hook_edges > 0 && !st.deb[2].open && now - st.hook_last > HOOK_QUIET_US) {
        printf("hook chatter, no change: %d edge%s over %" PRId64 " us, still %d\n", st.hook_edges,
               st.hook_edges == 1 ? "" : "s", st.hook_last - st.hook_first, st.deb[2].level);
        st.hook_edges = 0;
    }
}

static void print_raw(const dial_evt_t *e)
{
    const char *name = e->pin == PIN_HOOK ? "hook " : e->pin == PIN_PULSE ? "pulse" : "onorm";
    int64_t prev = last_raw_t[e->pin];
    last_raw_t[e->pin] = e->t_us;
    if (prev != 0) {
        printf("[%10" PRId64 " us] %s -> %d  (+%" PRId64 " us since the last %s edge)\n", e->t_us, name, e->level,
               e->t_us - prev, name);
    } else {
        printf("[%10" PRId64 " us] %s -> %d\n", e->t_us, name, e->level);
    }
}

// Contact chatter lasts microseconds to a few ms; a real dial break or make lasts tens of ms. A change is passed on
// (with the time of its first edge) only once the pad has held the new level for DEBOUNCE_US, so chatter inside a break
// or make is dropped instead of counted as extra pulses.
// The level the ISR reads right after an edge is not reliable: on a slow rising edge (a pin pulled up by the weak
// internal pull-up) it can still read the old level, and in a fast burst it can be stale (seen 2026-09-25: a clean
// pulse start read as 0, so the whole pulse was missed). So an edge only means "the pin changed here". Edges closer
// together than the debounce time form one burst; once the pin has been quiet that long the pad is read, and if it
// differs from the last accepted level that is a change, stamped with the first edge of the burst. A burst that ends
// where it began is chatter and is dropped.
static void debounce_edge(int i, int64_t t)
{
    if (!st.deb[i].open) {
        st.deb[i].open = true;
        st.deb[i].first_t = t;
    }
    st.deb[i].last_t = t;
}

static void debounce_resolve(int64_t now)
{
    const int pins[3] = {PIN_PULSE, PIN_ONORM, PIN_HOOK};
    for (int i = 0; i < 3; i++) {
        int64_t need = i == 2 ? HOOK_DEBOUNCE_US : DEBOUNCE_US;
        if (!st.deb[i].open || now - st.deb[i].last_t < need) {
            continue;
        }
        int level = gpio_get_level(pins[i]);
        st.deb[i].open = false;
        if (level == st.deb[i].level) {
            if (i < 2) {               // the hook's burst edges are reported by hook_report() instead
                st.glitches++;
                st.glitches_total++;
            }
            continue;
        }
        st.deb[i].level = level;
        if (i == 0) on_pulse(level, st.deb[i].first_t);
        else if (i == 1) on_onorm(level, st.deb[i].first_t);
        else hook_report(level);
    }
}

static void dial_task(void *arg)
{
    task_alive = true;
    dial_evt_t e;
    while (running) {
        if (xQueueReceive(dial_q, &e, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (st.raw) {
                print_raw(&e);
            }
            switch (e.pin) {
            case PIN_PULSE: debounce_edge(0, e.t_us); break;
            case PIN_ONORM: debounce_edge(1, e.t_us); break;
            case PIN_HOOK:  on_hook(e.level, e.t_us); debounce_edge(2, e.t_us); break;
            }
        } else {
            debounce_resolve(esp_timer_get_time());   // nothing waiting in the queue: judge the quiet bursts
        }
        tick(esp_timer_get_time());
    }
    task_alive = false;
    vTaskDelete(NULL);
}

static void read_rest(void)
{
    st.pulse_rest = gpio_get_level(PIN_PULSE);
    st.onorm_rest = gpio_get_level(PIN_ONORM);
    st.onorm_active = false;
    st.in_digit = false;
    st.onorm_seen = false;
    st.deb[0].level = st.pulse_rest;
    st.deb[1].level = st.onorm_rest;
    st.deb[2].level = gpio_get_level(PIN_HOOK);
    st.deb[0].open = st.deb[1].open = st.deb[2].open = false;
    printf("dial at rest: pulse (IO18) reads %d, off-normal (IO23) reads %d; a pulse is a change from %d on IO18, "
           "the dial is off home when IO23 is not %d\n", st.pulse_rest, st.onorm_rest, st.pulse_rest, st.onorm_rest);
    if (st.pulse_rest == st.onorm_rest) {
        printf("note: both contacts read the same at rest. A normal dial has the pulse contact closed (0) and the "
               "off-normal contact open (1) at rest. If the dial is not at home, or a contact is not wired, "
               "check with 'watch 18 23' while turning the dial.\n");
    }
}

static void dial_stop(void)
{
    gpio_tools_capture(NULL, 0, NULL);
    running = false;
    for (int i = 0; i < 20 && task_alive; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static int cmd_dial(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";

    if (!strcmp(sub, "on")) {
        if (running) {
            printf("already running; 'dial off' first, or 'dial rest' to re-read the rest levels\n");
            return 1;
        }
        bool raw = st.raw, poll = !polled_off;
        memset(&st, 0, sizeof(st));
        st.raw = raw;
        st.poll = poll;
        memset(last_raw_t, 0, sizeof(last_raw_t));
        gpio_tools_set_pull(PIN_PULSE, true);
        gpio_tools_set_pull(PIN_ONORM, true);
        gpio_tools_set_pull(PIN_HOOK, false);   // R15 on the board is the pull-down
        vTaskDelay(pdMS_TO_TICKS(20));          // let the pull-ups settle before reading
        read_rest();
        if (dial_q == NULL) {
            dial_q = xQueueCreate(128, sizeof(dial_evt_t));
        }
        xQueueReset(dial_q);
        running = true;
        xTaskCreate(dial_task, "dial", 4096, NULL, 4, NULL);
        const int capture[] = {PIN_HOOK, PIN_PULSE, PIN_ONORM};
        gpio_tools_capture(capture, 3, sink);
        printf("dial decoder on (settle-then-read debounce: dial %d ms, hook %d ms). The hook contact (IO13 / MTCK) reads %d now. Dial a digit, or lift and replace "
               "the handset. 'dial raw on' prints every edge; 'dial poll off' stops the state line.\n",
               DEBOUNCE_US / 1000, HOOK_DEBOUNCE_US / 1000, gpio_get_level(PIN_HOOK));
        return 0;
    }

    if (!strcmp(sub, "off")) {
        dial_stop();
        printf("dial decoder off\n");
        return 0;
    }

    if (!strcmp(sub, "raw")) {
        st.raw = argc >= 3 && !strcmp(argv[2], "on");
        printf("raw edges %s\n", st.raw ? "on" : "off");
        return 0;
    }

    if (!strcmp(sub, "poll")) {
        st.poll = !(argc >= 3 && !strcmp(argv[2], "off"));
        polled_off = !st.poll;
        printf("state line every %d ms %s\n", POLL_US / 1000, st.poll ? "on" : "off");
        return 0;
    }

    if (!strcmp(sub, "rest")) {
        if (!running) {
            printf("run 'dial on' first\n");
            return 1;
        }
        read_rest();
        return 0;
    }

    if (!strcmp(sub, "trials")) {
        if (!running) {
            printf("run 'dial on' first\n");
            return 1;
        }
        int n = argc >= 3 ? atoi(argv[2]) : 20;
        if (n < 1 || n > 500) {
            printf("usage: dial trials <1-500>\n");
            return 1;
        }
        memset(st.per_target, 0, sizeof(st.per_target));
        memset(st.per_target_ok, 0, sizeof(st.per_target_ok));
        st.bad_list[0] = 0;
        st.trial_ok = st.trial_bad = 0;
        st.trials_total = st.trials_left = n;
        printf("%d trials: dial each digit as asked, at any speed. 'dial off' cancels.\n", n);
        pick_target();
        return 0;
    }

    if (!strcmp(sub, "status")) {
        printf("decoder %s; IO13 / MTCK (hook) %d, IO18 (pulse) %d, IO23 (off-normal) %d\n", running ? "on" : "off",
               gpio_get_level(PIN_HOOK), gpio_get_level(PIN_PULSE), gpio_get_level(PIN_ONORM));
        printf("chatter edges ignored (held under %d ms): %d\n", DEBOUNCE_US / 1000, st.glitches_total);
        if (st.digits > 0) {
            printf("digits %d (bad %d, suspect bounce %d); break %.1f to %.1f ms, make %.1f to %.1f ms; "
                   "rate %.2f to %.2f/s; break ratio %.0f to %.0f%%\n", st.digits, st.bad_digits, st.suspect_digits,
                   st.brk_min / 1000.0, st.brk_max / 1000.0, st.make_min / 1000.0, st.make_max / 1000.0,
                   st.rate_min, st.rate_max, st.ratio_min, st.ratio_max);
        }
        if (st.trials_total > 0) {
            printf("trials: %d of %d done, %d right, %d wrong\n", st.trials_total - st.trials_left, st.trials_total,
                   st.trial_ok, st.trial_bad);
        }
        return 0;
    }

    printf("usage: dial on | off | rest | raw on|off | poll on|off | trials [n] | status\n");
    return 1;
}

void register_dial_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "dial",
        .help = "Rotary dial and hook decoder (stage 10): on, off, rest, raw on|off, poll on|off, trials [n], status",
        .func = cmd_dial,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
