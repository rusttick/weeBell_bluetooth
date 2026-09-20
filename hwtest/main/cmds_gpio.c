// Stage 4 GPIO tests: read candidate pins with no pulls, drive outputs, and log edges with
// microsecond timestamps. Only the pins in `pins[]` can be touched.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cmds.h"

typedef enum { M_IN, M_IN_PU, M_IN_PD, M_OUT } pin_mode_t;

typedef struct {
    int pin;
    const char *label;     // GPIO number and silkscreen name, so a pin can be found on the board
    const char *note;
    bool writable;         // false: read only (never driven)
    pin_mode_t mode;
    int out_level;
    int boot_level;        // level seen at the start of app_main, before we configured anything
    bool watch;            // print its edges
    int64_t win_start_us;  // one-second window used to catch a pin that is only picking up noise
    uint32_t win_count;
} pin_t;

// A floating input picks up mains hum and toggles constantly. Past this many edges in one second the
// watch on that pin is switched off instead of flooding the console.
#define MAX_EDGES_PER_SEC 200

#define PIN(n, lbl, nt, w) {.pin = (n), .label = (lbl), .note = (nt), .writable = (w), .mode = M_IN}

static pin_t pins[] = {
    PIN(13, "IO13/MTCK", "hook input: contact between 3V3 and MTCK, R15 10k pull-down, DIP SW4 ON", true),
    PIN(18, "IO18",      "dial pulse input (KEY5)", true),
    PIN(23, "IO23",      "dial-in-progress input (KEY4)", true),
    PIN(22, "IO22",      "STEP output; LED4 lights when low", true),
    PIN(19, "IO19",      "nENABLE output; LED5 lights when low (KEY3 is broken)", true),
    PIN(5,  "IO5",       "spare, strapping pin (KEY6)", true),
    PIN(21, "IO21",      "speaker amp enable (through R46; R51 holds it low); high = amps on", true),
};
#define NUM_PINS (sizeof(pins) / sizeof(pins[0]))

typedef struct {
    int pin;
    int level;
    int64_t t_us;
} edge_evt_t;

static QueueHandle_t edge_q;
static volatile uint32_t edges_dropped;

static pin_t *find_pin(int num)
{
    for (size_t i = 0; i < NUM_PINS; i++) {
        if (pins[i].pin == num) {
            return &pins[i];
        }
    }
    return NULL;
}

static const char *mode_str(pin_mode_t m)
{
    switch (m) {
    case M_IN:    return "in (no pull)";
    case M_IN_PU: return "in + pull-up";
    case M_IN_PD: return "in + pull-down";
    default:      return "out";
    }
}

static void IRAM_ATTR edge_isr(void *arg)
{
    edge_evt_t e = {.pin = (int)arg, .t_us = esp_timer_get_time()};
    e.level = gpio_get_level(e.pin);
    if (xQueueSendFromISR(edge_q, &e, NULL) != pdTRUE) {
        edges_dropped++;
    }
}

static void refresh_intr(const pin_t *p)
{
    if (p->watch && p->mode != M_OUT) {
        gpio_set_intr_type(p->pin, GPIO_INTR_ANYEDGE);
        gpio_intr_enable(p->pin);
    } else {
        gpio_intr_disable(p->pin);
    }
}

static void apply_mode(pin_t *p, pin_mode_t m, int level)
{
    if (m == M_OUT) {
        gpio_set_level(p->pin, level);  // set the output latch before the output turns on: no glitch
        p->out_level = level;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << p->pin,
        .mode = m == M_OUT ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT,  // input stays on so we can read the pad
        .pull_up_en = m == M_IN_PU ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = m == M_IN_PD ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    p->mode = m;
    refresh_intr(p);
}

static void edge_task(void *arg)
{
    edge_evt_t e;
    for (;;) {
        if (xQueueReceive(edge_q, &e, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        pin_t *p = find_pin(e.pin);
        if (p == NULL || !p->watch) {
            continue;  // left over in the queue after the watch was turned off
        }
        if (e.t_us - p->win_start_us > 1000000) {
            p->win_start_us = e.t_us;
            p->win_count = 0;
        }
        if (++p->win_count > MAX_EDGES_PER_SEC) {
            p->watch = false;
            gpio_intr_disable(p->pin);
            printf("%s: more than %d edges per second, watch turned off. It is probably floating and "
                   "picking up mains hum; try 'mode %d pu' first.\n", p->label, MAX_EDGES_PER_SEC, p->pin);
            continue;
        }
        printf("[%10" PRId64 " us] %-10s -> %d\n", e.t_us, p->label, e.level);
    }
}

void gpio_tools_init(void)
{
    // Read each pin as it was left at boot: input on, nothing else touched.
    for (size_t i = 0; i < NUM_PINS; i++) {
        gpio_pad_select_gpio(pins[i].pin);
        gpio_set_direction(pins[i].pin, GPIO_MODE_INPUT);
        pins[i].boot_level = gpio_get_level(pins[i].pin);
    }

    edge_q = xQueueCreate(256, sizeof(edge_evt_t));
    gpio_install_isr_service(0);
    for (size_t i = 0; i < NUM_PINS; i++) {
        apply_mode(&pins[i], M_IN, 0);
        gpio_isr_handler_add(pins[i].pin, edge_isr, (void *)pins[i].pin);
        gpio_intr_disable(pins[i].pin);
    }
    xTaskCreate(edge_task, "edges", 3072, NULL, 5, NULL);
}

bool gpio_tools_drive(int pin, int level)
{
    pin_t *p = find_pin(pin);
    if (p == NULL || !p->writable) {
        return false;
    }
    apply_mode(p, M_OUT, level);
    return true;
}

void gpio_tools_release(int pin)
{
    pin_t *p = find_pin(pin);
    if (p != NULL) {
        apply_mode(p, M_IN, 0);
    }
}

void gpio_tools_restore_floating(void)
{
    for (size_t i = 0; i < NUM_PINS; i++) {
        apply_mode(&pins[i], M_IN, 0);
    }
}

static int cmd_pins(int argc, char **argv)
{
    printf("%-4s %-10s %-15s %-5s %-5s %s\n", "pin", "label", "mode", "level", "boot", "note");
    for (size_t i = 0; i < NUM_PINS; i++) {
        const pin_t *p = &pins[i];
        printf("%-4d %-10s %-15s %-5d %-5d %s\n", p->pin, p->label, mode_str(p->mode),
               gpio_get_level(p->pin), p->boot_level, p->note);
    }
    printf("(level = the pad now; boot = level at the start of app_main, before any pulls were set)\n");
    return 0;
}

static int cmd_mode(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: mode <pin> <in|pu|pd|out> [0|1]   (out needs an explicit level)\n");
        return 1;
    }
    pin_t *p = find_pin(atoi(argv[1]));
    if (p == NULL) {
        printf("pin %s is not a test pin; run 'pins' to see the list\n", argv[1]);
        return 1;
    }
    pin_mode_t m;
    if (!strcmp(argv[2], "in")) {
        m = M_IN;
    } else if (!strcmp(argv[2], "pu")) {
        m = M_IN_PU;
    } else if (!strcmp(argv[2], "pd")) {
        m = M_IN_PD;
    } else if (!strcmp(argv[2], "out")) {
        m = M_OUT;
    } else {
        printf("mode must be in, pu, pd or out\n");
        return 1;
    }
    int level = 0;
    if (m == M_OUT) {
        if (!p->writable) {
            printf("%s is read only\n", p->label);
            return 1;
        }
        if (argc < 4 || (strcmp(argv[3], "0") && strcmp(argv[3], "1"))) {
            printf("out needs a level: mode %d out 0|1\n", p->pin);
            return 1;
        }
        level = atoi(argv[3]);
    }
    apply_mode(p, m, level);
    printf("%s: %s, pad reads %d\n", p->label, mode_str(m), gpio_get_level(p->pin));
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc < 3 || (strcmp(argv[2], "0") && strcmp(argv[2], "1"))) {
        printf("usage: set <pin> <0|1>\n");
        return 1;
    }
    pin_t *p = find_pin(atoi(argv[1]));
    if (p == NULL || p->mode != M_OUT) {
        printf("pin %s is not an output; use 'mode <pin> out <0|1>' first\n", argv[1]);
        return 1;
    }
    p->out_level = atoi(argv[2]);
    int64_t t0 = esp_timer_get_time();
    gpio_set_level(p->pin, p->out_level);
    // Time how long the pad takes to follow. A load with capacitance (or something fighting the pin) shows here.
    int64_t dt = 0;
    while (gpio_get_level(p->pin) != p->out_level && (dt = esp_timer_get_time() - t0) < 20000) {
    }
    if (gpio_get_level(p->pin) == p->out_level) {
        printf("%s: driven %d, pad followed after %" PRId64 " us\n", p->label, p->out_level, esp_timer_get_time() - t0);
    } else {
        printf("%s: driven %d, but the pad still reads %d after 20 ms: something is holding it\n",
               p->label, p->out_level, gpio_get_level(p->pin));
    }
    return 0;
}

static int cmd_watch(int argc, char **argv)
{
    if (argc >= 2) {
        if (!strcmp(argv[1], "off")) {
            for (size_t i = 0; i < NUM_PINS; i++) {
                pins[i].watch = false;
            }
        } else if (!strcmp(argv[1], "on") || !strcmp(argv[1], "all")) {
            for (size_t i = 0; i < NUM_PINS; i++) {
                pins[i].watch = true;
                pins[i].win_count = 0;
            }
        } else {
            // A list of pin numbers: watch exactly those.
            for (int a = 1; a < argc; a++) {
                if (find_pin(atoi(argv[a])) == NULL) {
                    printf("pin %s is not a test pin; run 'pins' to see the list\n", argv[a]);
                    return 1;
                }
            }
            for (size_t i = 0; i < NUM_PINS; i++) {
                pins[i].watch = false;
            }
            for (int a = 1; a < argc; a++) {
                pin_t *p = find_pin(atoi(argv[a]));
                p->watch = true;
                p->win_count = 0;
            }
        }
        for (size_t i = 0; i < NUM_PINS; i++) {
            refresh_intr(&pins[i]);
        }
    }
    printf("watching:");
    for (size_t i = 0; i < NUM_PINS; i++) {
        if (pins[i].watch) {
            printf(" %s%s", pins[i].label, pins[i].mode == M_OUT ? "(out, not reported)" : "");
        }
    }
    printf("\n%" PRIu32 " edges dropped\n", edges_dropped);
    return 0;
}

static int cmd_boot(int argc, char **argv)
{
    printf("levels at the start of app_main (input on, no pulls changed by us):\n");
    for (size_t i = 0; i < NUM_PINS; i++) {
        printf("  %-10s %d\n", pins[i].label, pins[i].boot_level);
    }
    return 0;
}

void register_gpio_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "pins", .help = "Show the test pins: mode, level now, level at boot", .func = cmd_pins},
        {.command = "mode", .help = "Set a pin: mode <pin> <in|pu|pd|out> [0|1]", .func = cmd_mode},
        {.command = "set", .help = "Drive an output: set <pin> <0|1>", .func = cmd_set},
        {.command = "watch", .help = "Print edges with a microsecond timestamp: watch on|all|off, or watch <pin> [<pin>...]. A pin with more than 200 edges/s is switched off.", .func = cmd_watch},
        {.command = "boot", .help = "Show the pin levels captured at the start of app_main", .func = cmd_boot},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
