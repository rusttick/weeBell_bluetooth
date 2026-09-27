// Stage 11 ring driver tests: drive the DRV8825 that rings the bell. STEP on IO22 comes from a hardware timer (LEDC), so
// the frequency is steady whatever the CPU is doing; nENABLE on IO19 gates the driver (active low, with an external
// 10k pull-up, so the bell is off at reset). With M0-M2 on GND the driver is in full-step mode and one phase drives
// the bell: the phase reverses every second step, so the output frequency is the STEP frequency divided by 4.
//
// Safety: nothing runs until 'ring on', 'ring sweep' or 'ring cadence' is typed; every run has a time limit and ends
// with nENABLE high; 'ring off' stops at once. The 40 V boost side is dangerous: keep it insulated, and start with the
// boost trimmed low.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "cmds.h"

#define PIN_STEP        22
#define PIN_NEN         19
#define STEPS_PER_CYCLE 4                    // full-step, one phase in use
#define MIN_HZ          5.0f
#define MAX_HZ          60.0f
#define DEFAULT_HZ      20.0f
#define DEFAULT_ON_S    2
#define MAX_ON_S        30
#define MAX_TASK_S      120                  // a sweep or cadence run may not be longer than this
#define LEDC_MODE       LEDC_HIGH_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_RES        LEDC_TIMER_13_BIT    // 13 bits keeps the lowest STEP frequency (20 Hz) reachable from the 80 MHz clock
#define LEDC_DUTY_50    (1 << 12)

static bool ledc_ready;
static float cur_hz = DEFAULT_HZ;
static volatile bool running;                // STEP is being generated
static volatile bool busy;                   // a worker task owns the driver
static volatile bool abort_req;

typedef enum { RUN_ON, RUN_SWEEP, RUN_CADENCE, RUN_HOLD } run_kind_t;
static struct {
    run_kind_t kind;
    float seconds;                           // RUN_ON
    float from_hz, to_hz, step_hz, dwell_s;  // RUN_SWEEP
    int cycles;                              // RUN_CADENCE
    bool uk;
} job;

static bool step_setup(void)
{
    if (ledc_ready) {
        return true;
    }
    ledc_timer_config_t t = {
        .speed_mode = LEDC_MODE, .duty_resolution = LEDC_RES, .timer_num = LEDC_TIMER,
        .freq_hz = (uint32_t)(cur_hz * STEPS_PER_CYCLE), .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t c = {
        .gpio_num = PIN_STEP, .speed_mode = LEDC_MODE, .channel = LEDC_CHANNEL, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0,
    };
    if (ledc_timer_config(&t) != ESP_OK || ledc_channel_config(&c) != ESP_OK) {
        printf("ring: LEDC setup failed\n");
        return false;
    }
    ledc_ready = true;
    return true;
}

static void step_stop(void)
{
    if (ledc_ready) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);   // STEP idles low
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
    running = false;
}

static bool step_start(float hz)
{
    if (!step_setup()) {
        return false;
    }
    if (ledc_set_freq(LEDC_MODE, LEDC_TIMER, (uint32_t)(hz * STEPS_PER_CYCLE + 0.5f)) != ESP_OK) {
        printf("ring: cannot make %.1f Hz\n", hz);
        return false;
    }
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_50);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    running = true;
    return true;
}

// nENABLE low = driver on. The latch is set before the pin becomes an output (gpio_tools_drive), so no glitch.
static void driver_enable(bool on)
{
    gpio_tools_drive(PIN_NEN, on ? 0 : 1);
}

// Sleep in short slices so 'ring off' is heard within 20 ms. Returns false if aborted.
static bool wait_s(float s)
{
    int slices = (int)(s * 50.0f);
    for (int i = 0; i < slices; i++) {
        if (abort_req) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return !abort_req;
}

static bool burst(float hz, float seconds)
{
    driver_enable(true);
    vTaskDelay(pdMS_TO_TICKS(5));                    // the driver needs a moment after nENABLE falls
    if (!step_start(hz)) {
        return false;
    }
    bool ok = wait_s(seconds);
    step_stop();
    driver_enable(false);
    return ok;
}

static void ring_task(void *arg)
{
    switch (job.kind) {
    case RUN_ON:
        printf("ring: %.1f Hz (STEP %.0f Hz) for %.1f s\n", cur_hz, cur_hz * STEPS_PER_CYCLE, job.seconds);
        burst(cur_hz, job.seconds);
        break;
    case RUN_HOLD:
        // No STEP pulses: the driver holds one polarity on A1/A2, so a floating meter on DC volts reads the coil voltage.
        printf("ring hold: driver on, STEP stopped, for %.0f s. Read DC volts across A1 and A2 now.\n", job.seconds);
        driver_enable(true);
        wait_s(job.seconds);
        break;
    case RUN_SWEEP:
        for (float hz = job.from_hz; hz <= job.to_hz + 0.001f && !abort_req; hz += job.step_hz) {
            printf("ring sweep: %.1f Hz\n", hz);
            cur_hz = hz;
            if (!burst(hz, job.dwell_s)) {
                break;
            }
            wait_s(0.3f);                            // a short gap so each frequency can be heard on its own
        }
        break;
    case RUN_CADENCE:
        for (int i = 0; i < job.cycles && !abort_req; i++) {
            printf("ring cadence %d of %d (%s) at %.1f Hz\n", i + 1, job.cycles, job.uk ? "UK double" : "2 s / 4 s",
                   cur_hz);
            bool ok;
            if (job.uk) {
                ok = burst(cur_hz, 0.4f) && wait_s(0.2f) && burst(cur_hz, 0.4f) && wait_s(2.0f);
            } else {
                ok = burst(cur_hz, 2.0f) && wait_s(4.0f);
            }
            if (!ok) {
                break;
            }
        }
        break;
    }
    step_stop();
    driver_enable(false);
    printf("ring: %s, driver off (nENABLE high)\n", abort_req ? "stopped" : "done");
    busy = false;
    vTaskDelete(NULL);
}

static bool parse_hz(const char *s, float *hz)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s || v < MIN_HZ || v > MAX_HZ) {
        printf("frequency must be %.0f to %.0f Hz (the output frequency; STEP is %d times that)\n", MIN_HZ, MAX_HZ,
               STEPS_PER_CYCLE);
        return false;
    }
    *hz = v;
    return true;
}

static bool start_job(void)
{
    if (busy) {
        printf("ring: already running; 'ring off' first\n");
        return false;
    }
    abort_req = false;
    busy = true;
    if (xTaskCreate(ring_task, "ring", 4096, NULL, 5, NULL) != pdPASS) {
        busy = false;
        printf("ring: cannot start the task\n");
        return false;
    }
    return true;
}

static int cmd_ring(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";

    if (!strcmp(sub, "off")) {
        abort_req = true;
        for (int i = 0; i < 50 && busy; i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        step_stop();
        driver_enable(false);
        printf("ring off (STEP stopped, nENABLE high)\n");
        return 0;
    }

    if (!strcmp(sub, "status")) {
        printf("ring: %s, output %.1f Hz (STEP %.0f Hz), IO19 nENABLE reads %d, IO22 STEP reads %d\n",
               busy ? (running ? "ringing" : "running a sequence") : "idle", cur_hz, cur_hz * STEPS_PER_CYCLE,
               gpio_get_level(PIN_NEN), gpio_get_level(PIN_STEP));
        return 0;
    }

    if (!strcmp(sub, "freq")) {
        float hz;
        if (argc < 3 || !parse_hz(argv[2], &hz)) {
            printf("usage: ring freq <%.0f-%.0f>   (now %.1f Hz)\n", MIN_HZ, MAX_HZ, cur_hz);
            return 1;
        }
        cur_hz = hz;
        if (running) {
            ledc_set_freq(LEDC_MODE, LEDC_TIMER, (uint32_t)(hz * STEPS_PER_CYCLE + 0.5f));
        }
        printf("output %.1f Hz (STEP %.0f Hz)\n", hz, hz * STEPS_PER_CYCLE);
        return 0;
    }

    if (!strcmp(sub, "on")) {
        float seconds = argc >= 3 ? strtof(argv[2], NULL) : DEFAULT_ON_S;
        if (seconds <= 0 || seconds > MAX_ON_S) {
            printf("usage: ring on [seconds 0-%d] [Hz]\n", MAX_ON_S);
            return 1;
        }
        if (argc >= 4 && !parse_hz(argv[3], &cur_hz)) {
            return 1;
        }
        job.kind = RUN_ON;
        job.seconds = seconds;
        return start_job() ? 0 : 1;
    }

    if (!strcmp(sub, "hold")) {
        float seconds = argc >= 3 ? strtof(argv[2], NULL) : 10.0f;
        if (seconds <= 0 || seconds > 20) {
            printf("usage: ring hold [seconds 1-20 = 10]   (holds one polarity on A1/A2 for a DC meter reading)\n");
            return 1;
        }
        job.kind = RUN_HOLD;
        job.seconds = seconds;
        return start_job() ? 0 : 1;
    }

    if (!strcmp(sub, "sweep")) {
        float from, to;
        if (argc < 4 || !parse_hz(argv[2], &from) || !parse_hz(argv[3], &to) || to < from) {
            printf("usage: ring sweep <from Hz> <to Hz> [dwell s = 1] [step Hz = 1]\n");
            return 1;
        }
        job.dwell_s = argc >= 5 ? strtof(argv[4], NULL) : 1.0f;
        job.step_hz = argc >= 6 ? strtof(argv[5], NULL) : 1.0f;
        if (job.dwell_s < 0.2f || job.step_hz < 0.1f) {
            printf("dwell must be at least 0.2 s and step at least 0.1 Hz\n");
            return 1;
        }
        float total = ((to - from) / job.step_hz + 1) * (job.dwell_s + 0.3f);
        if (total > MAX_TASK_S) {
            printf("that sweep would take %.0f s; the limit is %d s\n", total, MAX_TASK_S);
            return 1;
        }
        job.kind = RUN_SWEEP;
        job.from_hz = from;
        job.to_hz = to;
        printf("sweep %.1f to %.1f Hz, about %.0f s; 'ring off' stops it\n", from, to, total);
        return start_job() ? 0 : 1;
    }

    if (!strcmp(sub, "cadence")) {
        int cycles = 3;
        job.uk = false;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "uk")) {
                job.uk = true;
            } else {
                cycles = atoi(argv[i]);
            }
        }
        if (cycles < 1 || cycles > 20) {
            printf("usage: ring cadence [cycles 1-20 = 3] [uk]\n");
            return 1;
        }
        job.kind = RUN_CADENCE;
        job.cycles = cycles;
        return start_job() ? 0 : 1;
    }

    printf("usage: ring on [seconds] [Hz] | hold [s] | freq <Hz> | sweep <from> <to> [dwell s] [step Hz] | cadence [n] [uk] | "
           "off | status\n");
    return 1;
}

void register_ring_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ring",
        .help = "Ring driver (stage 11): DRV8825 STEP on IO22 (LEDC), nENABLE on IO19. ring on [s] [Hz] | freq <Hz> | "
                "sweep <from> <to> [dwell s] [step Hz] | cadence [n] [uk] | off | status. Every run has a time limit; "
                "the boost side is 40 V, keep it insulated.",
        .func = cmd_ring,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
