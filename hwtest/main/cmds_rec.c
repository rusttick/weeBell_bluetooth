// `rec`: record the microphone to a WAV file on the SD card (mono, 16-bit, at the codec's current sample rate).
//
// The audio is captured into RAM (PSRAM) and only written to the card AFTER the capture ends. Reason (stage 9): writing to the
// card while recording put a burst of noise into the microphone every 16 ms (one 512-byte sector at 16 kHz mono), coupled
// through the shared 3.3 V supply and ground; a recording made that way was mostly that noise. The card is idle during capture.
// The limit is 60 s (PSRAM). The recording goes through the microphone equalizer profile if one is selected (`eqp mic <n>`);
// select 0 (flat) for a raw recording.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_console.h"
#include "cmds.h"
#include "eq.h"

#define MAX_SECONDS 300         // an upper bound; the real limit is the free PSRAM (about 110 s at 16 kHz, 220 s at 8 kHz)

static volatile bool rec_stop, rec_active, rec_saving;
static volatile uint32_t rec_frames, rec_peak;
static volatile uint64_t rec_sumsq;
static int rec_rate;
static uint32_t rec_limit_frames;
static char rec_path[300];
static int16_t *rec_buf;

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void rec_task(void *arg)
{
    static int16_t st[64 * 2];
    rec_active = true;
    uint32_t n = 0, peak = 0;
    uint64_t sum = 0;
    while (!rec_stop && n < rec_limit_frames) {
        int got = audio_read_frames(st, 64, 200);               // through the microphone equalizer profile, if any
        if (got <= 0) {
            continue;
        }
        if (n + (uint32_t)got > rec_limit_frames) {
            got = (int)(rec_limit_frames - n);
        }
        for (int i = 0; i < got; i++) {
            int16_t s = st[2 * i];
            rec_buf[n + (uint32_t)i] = s;
            uint32_t a = (uint32_t)(s < 0 ? -(int)s : s);
            if (a > peak) {
                peak = a;
            }
            sum += (uint64_t)((int)s * (int)s);
        }
        n += (uint32_t)got;
        rec_frames = n;
    }
    rec_peak = peak;
    rec_sumsq = sum;

    // Capture is finished: now, and only now, touch the card.
    rec_saving = true;
    printf("capture finished (%.1f s); writing %s ...\n", (double)n / rec_rate, rec_path);
    int fd = open(rec_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    uint32_t bytes = n * 2;
    bool ok = fd >= 0;
    if (ok) {
        uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
        put32(h + 4, 36 + bytes);
        put32(h + 16, 16);
        put16(h + 20, 1);
        put16(h + 22, 1);
        put32(h + 24, (uint32_t)rec_rate);
        put32(h + 28, (uint32_t)rec_rate * 2u);
        put16(h + 32, 2);
        put16(h + 34, 16);
        memcpy(h + 36, "data", 4);
        put32(h + 40, bytes);
        ok = write(fd, h, sizeof(h)) == (ssize_t)sizeof(h);
        const uint8_t *p = (const uint8_t *)rec_buf;
        for (uint32_t off = 0; ok && off < bytes; off += 16384) {
            uint32_t len = bytes - off > 16384 ? 16384 : bytes - off;
            ok = write(fd, p + off, len) == (ssize_t)len;
        }
        fsync(fd);
        close(fd);
    }
    double rms = n ? sqrt((double)sum / n) : 0.0;
    printf("recording %s: %s, %.1f s at %d Hz, %u bytes. Level: peak %.1f dBFS, average %.1f dBFS. Microphone profile: %d (%s)\n",
           ok ? "saved" : "FAILED to save", rec_path, (double)n / rec_rate, rec_rate, (unsigned)bytes,
           peak > 0 ? 20.0 * log10(peak / 32768.0) : -99.9, rms > 0 ? 20.0 * log10(rms / 32768.0) : -99.9, eq_selected(true),
           eq_mic_profiles[eq_selected(true)].name);
    heap_caps_free(rec_buf);
    rec_buf = NULL;
    rec_saving = false;
    rec_active = false;
    vTaskDelete(NULL);
}

static int rec_start(int argc, char **argv)
{
    if (rec_active) {
        printf("already recording: 'rec stop' first\n");
        return 1;
    }
    if (argc < 3) {
        printf("usage: rec start <name.wav> [seconds, default 10, max %d]\n", MAX_SECONDS);
        return 1;
    }
    if (!audio_is_on()) {
        printf("run 'audio on <rate>' first (8000 or 16000; the microphone is set up by it)\n");
        return 1;
    }
    if (!sd_ready(true)) {
        return 1;
    }
    rec_rate = audio_rate();
    // The whole recording is held in PSRAM until it ends, so the limit is the largest free block (minus a margin).
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    int max_secs = largest > 256 * 1024 ? (int)((largest - 256 * 1024) / ((size_t)rec_rate * 2)) : 0;
    if (max_secs > MAX_SECONDS) {
        max_secs = MAX_SECONDS;
    }
    int secs = argc >= 4 ? atoi(argv[3]) : 10;
    if (secs < 1 || secs > max_secs) {
        printf("seconds must be 1 to %d at %d Hz (the recording is held in RAM until it ends; %u KB free)\n", max_secs,
               rec_rate, (unsigned)(largest / 1024));
        return 1;
    }
    rec_limit_frames = (uint32_t)secs * (uint32_t)rec_rate;
    rec_buf = heap_caps_malloc((size_t)rec_limit_frames * 2, MALLOC_CAP_SPIRAM);
    if (!rec_buf) {
        printf("not enough RAM for %d s: try a shorter recording\n", secs);
        return 1;
    }
    if (argv[2][0] == '/') {
        snprintf(rec_path, sizeof(rec_path), "%s", argv[2]);
    } else {
        snprintf(rec_path, sizeof(rec_path), "/sdcard/%s", argv[2]);
    }
    rec_frames = rec_peak = 0;
    rec_sumsq = 0;
    rec_stop = false;
    xTaskCreatePinnedToCore(rec_task, "rec", 4096, NULL, 6, NULL, 1);
    printf("recording %d s at %d Hz into RAM (the card stays idle until it ends, then %s is written). Microphone profile %d (%s). "
           "'rec stop' ends it early.\n", secs, rec_rate, rec_path, eq_selected(true), eq_mic_profiles[eq_selected(true)].name);
    return 0;
}

static int cmd_rec(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "start")) {
        return rec_start(argc, argv);
    } else if (argc >= 2 && !strcmp(argv[1], "stop")) {
        if (!rec_active) {
            printf("not recording\n");
            return 0;
        }
        rec_stop = true;
        return 0;
    } else if (argc >= 2 && !strcmp(argv[1], "status")) {
        if (rec_saving) {
            printf("writing %s to the card\n", rec_path);
        } else if (rec_active) {
            printf("recording %s: %.1f of %.1f s\n", rec_path, (double)rec_frames / rec_rate, (double)rec_limit_frames / rec_rate);
        } else {
            printf("not recording\n");
        }
        return 0;
    }
    printf("usage: rec start <name.wav> [seconds, max %d] | stop | status\n"
           "  records the microphone (after the 'eqp mic' profile) into RAM, then writes it to the SD card;\n"
           "  needs 'audio on' and 'sd mount rw'\n", MAX_SECONDS);
    return 1;
}

void register_rec_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "rec",
        .help = "Record the microphone to the SD card (captured in RAM first): rec start <name.wav> [seconds] | stop | status",
        .func = cmd_rec,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
