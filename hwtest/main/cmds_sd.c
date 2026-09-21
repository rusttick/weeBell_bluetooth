// Stage 8: microSD card for voice prompts (doc/validate_board_plan.md, stage 8).
//
// 1-bit SDMMC mode on slot 1: CLK = IO14, CMD = IO15, DATA0 = IO2 (fixed by the ESP32). DIP: SW3 ON (IO15 to CMD),
// SW2 OFF (DATA3 stays pulled up on the card side). Card-detect is on IO34.
//
// NEVER FORMATS: format_if_mount_failed is false and nothing here calls a format function. The card mounts read-only
// by policy (write commands refuse) unless you type `sd mount rw`.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_console.h"
#include "esp_system.h"     // esp_random() in ESP-IDF 4.4
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "cmds.h"

#define MOUNT        "/sdcard"
#define CD_PIN       GPIO_NUM_34
#define IOBUF_SIZE   (64 * 1024)
#define TEST_FILE    MOUNT "/hwtest.bin"
#define SPEED_FILE   MOUNT "/speed.bin"
#define BANK_FILE    MOUNT "/bank.bin"
#define SCALE_DIR    MOUNT "/scale"

static sdmmc_card_t *card;
static bool sd_rw;

static uint8_t *io_buf(void)
{
    static uint8_t *b;
    if (!b) {
        b = malloc(IOBUF_SIZE);
    }
    return b;
}

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

// Deterministic content, so a file can be checked later without keeping a copy.
static uint8_t pat(uint32_t off)
{
    return (uint8_t)(off * 31u + (off >> 9));
}

static void fill_pattern(uint8_t *b, uint32_t off, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        b[i] = pat(off + (uint32_t)i);
    }
}

static bool need(bool write)
{
    if (!card) {
        printf("no card mounted: type 'sd mount' first\n");
        return false;
    }
    if (write && !sd_rw) {
        printf("this command writes to the card: 'sd unmount', then 'sd mount rw'\n");
        return false;
    }
    if (!io_buf()) {
        printf("out of memory\n");
        return false;
    }
    return true;
}

// For other commands (the recorder): is a card mounted, and mounted read-write if `write`? Prints why not.
bool sd_ready(bool write)
{
    if (!card) {
        printf("no card mounted: type 'sd mount rw' first\n");
        return false;
    }
    if (write && !sd_rw) {
        printf("the card is mounted read-only: 'sd unmount', then 'sd mount rw'\n");
        return false;
    }
    return true;
}

static void resolve(char *out, size_t n, const char *arg)
{
    if (arg[0] == '/') {
        snprintf(out, n, "%s", arg);
    } else {
        snprintf(out, n, "%s/%s", MOUNT, arg);
    }
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

// Timing statistics in milliseconds. Sorts the array.
static void print_stats(const char *what, uint32_t *us, int n)
{
    if (n <= 0) {
        return;
    }
    double sum = 0;
    for (int i = 0; i < n; i++) {
        sum += us[i];
    }
    qsort(us, (size_t)n, sizeof(uint32_t), cmp_u32);
    printf("%s: %d samples, avg %.3f ms, median %.3f ms, 95th percentile %.3f ms, max %.3f ms\n", what, n, sum / n / 1000.0,
           us[n / 2] / 1000.0, us[(int)((n - 1) * 0.95)] / 1000.0, us[n - 1] / 1000.0);
}

// ---- mount, unmount, card detect ----
static int sd_mount(int argc, char **argv)
{
    if (card) {
        printf("already mounted; 'sd unmount' first\n");
        return 1;
    }
    bool rw = false, pu = false;
    int khz = SDMMC_FREQ_DEFAULT;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "rw")) {
            rw = true;
        } else if (!strcmp(argv[i], "pu")) {
            pu = true;
        } else if (atoi(argv[i]) > 0) {
            khz = atoi(argv[i]);
        } else {
            printf("usage: sd mount [rw] [pu] [kHz]   (rw = allow writes, pu = internal pull-ups, kHz e.g. 400, 20000, 40000)\n");
            return 1;
        }
    }
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,   // NEVER format the card
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = khz;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    if (pu) {
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    }
    int64_t t0 = now_us();
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mc, &card);
    if (err != ESP_OK) {
        card = NULL;
        printf("mount FAILED: %s (0x%x) after %.0f ms\n", esp_err_to_name(err), err, (now_us() - t0) / 1000.0);
        printf("  ESP_FAIL = the card is not FAT (it is NOT formatted by this tool); timeout or 'not supported' = no card, a poor\n"
               "  contact, or wiring; try 'sd mount pu' or a lower speed such as 'sd mount 400'.\n");
        return 1;
    }
    sd_rw = rw;
    printf("mounted in %.0f ms, 1-bit mode, %d kHz, %s\n", (now_us() - t0) / 1000.0, khz,
           rw ? "READ-WRITE" : "read-only by policy");
    sdmmc_card_print_info(stdout, card);
    FATFS *fs = NULL;
    DWORD free_clust = 0;
    if (f_getfree("0:", &free_clust, &fs) == FR_OK && fs) {
        uint64_t sec = card->csd.sector_size ? card->csd.sector_size : 512;
        uint64_t tot = (uint64_t)(fs->n_fatent - 2) * fs->csize * sec;
        uint64_t fre = (uint64_t)free_clust * fs->csize * sec;
        printf("filesystem: FAT%s, cluster %u bytes, %.1f MB total, %.1f MB free\n",
               fs->fs_type == FS_FAT32 ? "32" : fs->fs_type == FS_FAT16 ? "16" : fs->fs_type == FS_FAT12 ? "12" : "?(exFAT?)",
               (unsigned)(fs->csize * sec), tot / 1048576.0, fre / 1048576.0);
    }
    return 0;
}

static int sd_unmount(void)
{
    if (!card) {
        printf("not mounted\n");
        return 0;
    }
    esp_vfs_fat_sdcard_unmount(MOUNT, card);
    card = NULL;
    printf("unmounted\n");
    return 0;
}

static int sd_cd(void)
{
    gpio_config_t io = {.pin_bit_mask = 1ULL << CD_PIN, .mode = GPIO_MODE_INPUT};
    gpio_config(&io);
    printf("card-detect IO34 = %d (insert or remove the card and repeat to see which level means 'inserted')\n",
           gpio_get_level(CD_PIN));
    return 0;
}

// ---- listing ----
static int sd_ls(int argc, char **argv)
{
    if (!need(false)) {
        return 1;
    }
    char path[300];
    resolve(path, sizeof(path), argc >= 3 ? argv[2] : MOUNT);
    int limit = argc >= 4 ? atoi(argv[3]) : 40;
    DIR *d = opendir(path);
    if (!d) {
        printf("cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }
    int64_t t0 = now_us();
    int files = 0, dirs = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char full[600];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        bool isdir = e->d_type == DT_DIR;
        long size = 0;
        if (!isdir && stat(full, &st) == 0) {
            size = (long)st.st_size;
        }
        if (isdir) {
            dirs++;
        } else {
            files++;
        }
        if (files + dirs <= limit) {
            printf("  %s%s  %ld\n", e->d_name, isdir ? "/" : "", size);
        }
    }
    closedir(d);
    printf("%s: %d files, %d directories (listing and stat took %.1f ms)%s\n", path, files, dirs, (now_us() - t0) / 1000.0,
           files + dirs > limit ? "; only the first entries were printed" : "");
    return 0;
}

// ---- write / read-back test and later check ----
static int verify_pattern(const char *path, long *size, long *bad)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    uint8_t *b = io_buf();
    static uint8_t *ref;
    if (!ref) {
        ref = malloc(IOBUF_SIZE);
    }
    if (!ref) {
        fclose(f);
        return -2;
    }
    long off = 0;
    *bad = 0;
    size_t n;
    while ((n = fread(b, 1, IOBUF_SIZE, f)) > 0) {
        fill_pattern(ref, (uint32_t)off, n);
        for (size_t i = 0; i < n; i++) {
            if (b[i] != ref[i]) {
                (*bad)++;
            }
        }
        off += (long)n;
    }
    fclose(f);
    *size = off;
    return 0;
}

static int sd_test(void)
{
    if (!need(true)) {
        return 1;
    }
    const long total = 256 * 1024;
    uint8_t *b = io_buf();
    FILE *f = fopen(TEST_FILE, "wb");
    if (!f) {
        printf("cannot create %s: %s\n", TEST_FILE, strerror(errno));
        return 1;
    }
    int64_t t0 = now_us();
    for (long off = 0; off < total; off += IOBUF_SIZE) {
        fill_pattern(b, (uint32_t)off, IOBUF_SIZE);
        if (fwrite(b, 1, IOBUF_SIZE, f) != IOBUF_SIZE) {
            printf("write failed at %ld: %s\n", off, strerror(errno));
            fclose(f);
            return 1;
        }
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    printf("wrote %ld bytes in %.1f ms\n", total, (now_us() - t0) / 1000.0);
    long size, bad;
    int r = verify_pattern(TEST_FILE, &size, &bad);
    if (r != 0) {
        printf("read-back failed to open the file\n");
        return 1;
    }
    printf("read back %ld bytes, %ld wrong: %s\n", size, bad, (bad == 0 && size == total) ? "PASS" : "FAIL");
    return bad == 0 && size == total ? 0 : 1;
}

// After a power cycle: check the file from `sd test` is still intact. Writes nothing.
static int sd_check(void)
{
    if (!need(false)) {
        return 1;
    }
    long size, bad;
    int r = verify_pattern(TEST_FILE, &size, &bad);
    if (r == -1) {
        printf("%s not found: run 'sd test' (after 'sd mount rw') once first\n", TEST_FILE);
        return 1;
    }
    printf("check: %ld bytes, %ld wrong: %s\n", size, bad, (bad == 0 && size > 0) ? "PASS" : "FAIL");
    return bad == 0 && size > 0 ? 0 : 1;
}

// ---- speed and latency ----
// Read a file in chunks and time each read. posix = plain open()/read() instead of stdio fread(): stdio refills a small
// internal buffer, so a big fread still becomes many small card reads.
static int read_pass(const char *path, size_t chunk, long total, const char *label, bool posix)
{
    FILE *f = posix ? NULL : fopen(path, "rb");
    int fd = posix ? open(path, O_RDONLY) : -1;
    if (!f && fd < 0) {
        printf("cannot open %s\n", path);
        return 1;
    }
    int max = (int)(total / (long)chunk) + 1;
    uint32_t *us = malloc(sizeof(uint32_t) * (size_t)max);
    if (!us) {
        if (f) {
            fclose(f);
        } else {
            close(fd);
        }
        printf("out of memory\n");
        return 1;
    }
    uint8_t *b = io_buf();
    int n = 0, slow = 0;
    long got_total = 0;
    int64_t t_all = now_us();
    while (got_total < total && n < max) {
        int64_t t0 = now_us();
        size_t got;
        if (posix) {
            ssize_t r = read(fd, b, chunk);
            got = r > 0 ? (size_t)r : 0;
        } else {
            got = fread(b, 1, chunk, f);
        }
        us[n] = (uint32_t)(now_us() - t0);
        if (us[n] > 20000) {
            slow++;
        }
        n++;
        got_total += (long)got;
        if (got < chunk) {
            break;
        }
    }
    double secs = (now_us() - t_all) / 1e6;
    if (f) {
        fclose(f);
    } else {
        close(fd);
    }
    printf("%s: %.0f KB/s over %ld bytes; %d reads slower than 20 ms\n", label, got_total / 1024.0 / secs, got_total, slow);
    print_stats("  per-read time", us, n);
    free(us);
    return 0;
}

static int sd_speed(int mb)
{
    if (!need(false)) {
        return 1;
    }
    if (mb < 1 || mb > 32) {
        mb = 4;
    }
    const long total = (long)mb * 1024 * 1024;
    struct stat st;
    if (stat(SPEED_FILE, &st) != 0 || st.st_size < total) {
        if (!need(true)) {
            return 1;
        }
        uint8_t *b = io_buf();
        FILE *f = fopen(SPEED_FILE, "wb");
        if (!f) {
            printf("cannot create %s: %s\n", SPEED_FILE, strerror(errno));
            return 1;
        }
        fill_pattern(b, 0, IOBUF_SIZE);
        int64_t t0 = now_us();
        for (long off = 0; off < total; off += IOBUF_SIZE) {
            if (fwrite(b, 1, IOBUF_SIZE, f) != IOBUF_SIZE) {
                printf("write failed: %s\n", strerror(errno));
                fclose(f);
                return 1;
            }
        }
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        printf("write: %.0f KB/s (%d MB in %.2f s)\n", total / 1024.0 / ((now_us() - t0) / 1e6), mb, (now_us() - t0) / 1e6);
    }
    printf("voice at 16 kHz, 16-bit mono needs about 32 KB/s (8 kHz needs 16); the goal here is latency and hiccups.\n");
    read_pass(SPEED_FILE, 64 * 1024, total, "stdio fread, 64 KB chunks", false);
    read_pass(SPEED_FILE, 4 * 1024, total, "stdio fread, 4 KB chunks (streaming size)", false);
    read_pass(SPEED_FILE, 64 * 1024, total, "POSIX read(), 64 KB chunks", true);
    read_pass(SPEED_FILE, 4 * 1024, total, "POSIX read(), 4 KB chunks (streaming size)", true);
    return 0;
}

// ---- WAV generation and playback ----
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

// Write a mono 16-bit test WAV: a 1 kHz sine at -12 dBFS.
static int sd_gen(int argc, char **argv)
{
    if (!need(true)) {
        return 1;
    }
    if (argc < 5) {
        printf("usage: sd gen <name.wav> <rate> <seconds> [silence]   (silence = digital zeros, to test for noise from SD reads)\n");
        return 1;
    }
    const bool silence = argc >= 6 && !strcmp(argv[5], "silence");
    char path[300];
    resolve(path, sizeof(path), argv[2]);
    int rate = atoi(argv[3]), secs = atoi(argv[4]);
    if (rate < 8000 || rate > 48000 || secs < 1 || secs > 120) {
        printf("rate 8000 to 48000, seconds 1 to 120\n");
        return 1;
    }
    uint32_t data = (uint32_t)rate * (uint32_t)secs * 2u;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    put32(h + 4, 36 + data);
    put32(h + 16, 16);
    put16(h + 20, 1);              // PCM
    put16(h + 22, 1);              // mono
    put32(h + 24, (uint32_t)rate);
    put32(h + 28, (uint32_t)rate * 2u);
    put16(h + 32, 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, data);
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("cannot create %s: %s\n", path, strerror(errno));
        return 1;
    }
    fwrite(h, 1, sizeof(h), f);
    int16_t *s = (int16_t *)io_buf();
    const int per = IOBUF_SIZE / 2;
    long done = 0, count = (long)rate * secs;
    float phase = 0.0f, inc = 2.0f * (float)M_PI * 1000.0f / (float)rate;
    while (done < count) {
        int n = count - done < per ? (int)(count - done) : per;
        for (int i = 0; i < n; i++) {
            s[i] = silence ? 0 : (int16_t)(sinf(phase) * 0.25f * 32767.0f);
            phase += inc;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        fwrite(s, 2, (size_t)n, f);
        done += n;
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    printf("wrote %s: %d Hz mono, %d s, %s\n", path, rate, secs, silence ? "digital silence" : "1 kHz sine at -12 dBFS");
    return 0;
}

typedef struct {
    int channels;
    int rate;
    long data_off;
    uint32_t data_len;
} wav_t;

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

static bool wav_parse(FILE *f, wav_t *w)
{
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        return false;
    }
    bool fmt = false;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) {
            return false;
        }
        uint32_t len = rd32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t b[16];
            if (len < 16 || fread(b, 1, 16, f) != 16) {
                return false;
            }
            if (rd16(b) != 1 || rd16(b + 14) != 16) {
                printf("only 16-bit PCM WAV files are supported\n");
                return false;
            }
            w->channels = (int)rd16(b + 2);
            w->rate = (int)rd32(b + 4);
            fmt = true;
            fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            w->data_off = ftell(f);
            w->data_len = len;
            long here = ftell(f);
            fseek(f, 0, SEEK_END);
            long remain = ftell(f) - here;
            fseek(f, here, SEEK_SET);
            if ((long)w->data_len > remain) {
                w->data_len = (uint32_t)remain;
            }
            return fmt && (w->channels == 1 || w->channels == 2);
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
}

// Stream a WAV from the card to the codec and time the reads. The read that follows a write must finish before the I2S
// buffers (about 768 frames) run dry, or the audio drops out.
static int sd_play(int argc, char **argv)
{
    if (!need(false)) {
        return 1;
    }
    if (argc < 3) {
        printf("usage: sd play <name.wav> [frames per read, default 2048]\n");
        return 1;
    }
    if (!audio_is_on()) {
        printf("run 'audio on <rate>' first (and 'audio out spk' for the earpiece; keep 'vol' low)\n");
        return 1;
    }
    char path[300];
    resolve(path, sizeof(path), argv[2]);
    int frames = argc >= 4 ? atoi(argv[3]) : 2048;
    if (frames < 256 || frames > 8192) {
        frames = 2048;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }
    wav_t w = {0};
    if (!wav_parse(f, &w)) {
        printf("%s is not a usable 16-bit mono or stereo PCM WAV\n", path);
        fclose(f);
        return 1;
    }
    if (w.rate != audio_rate()) {
        printf("%s is %d Hz but audio is running at %d Hz: type 'audio on %d' first\n", path, w.rate, audio_rate(), w.rate);
        fclose(f);
        return 1;
    }
    int16_t *in = malloc((size_t)frames * (size_t)w.channels * 2);
    int16_t *out = malloc((size_t)frames * 4);
    if (!in || !out) {
        printf("out of memory\n");
        free(in);
        free(out);
        fclose(f);
        return 1;
    }
    const uint32_t budget_us = (uint32_t)(768000000LL / w.rate);
    fseek(f, w.data_off, SEEK_SET);
    uint32_t left = w.data_len;
    uint32_t *us = malloc(sizeof(uint32_t) * (size_t)(w.data_len / ((uint32_t)frames * (uint32_t)w.channels * 2) + 2));
    int n = 0, over = 0;
    printf("playing %s: %d Hz, %d channel(s), %.1f s; read budget %.1f ms per chunk\n", path, w.rate, w.channels,
           (double)w.data_len / (w.rate * w.channels * 2), budget_us / 1000.0);
    audio_source_pause();
    while (left > 0) {
        size_t want = (size_t)frames * (size_t)w.channels * 2;
        if (want > left) {
            want = left;
        }
        int64_t t0 = now_us();
        size_t got = fread(in, 1, want, f);
        int nf = (int)(got / ((size_t)w.channels * 2));
        if (nf <= 0) {
            break;
        }
        for (int i = 0; i < nf; i++) {
            if (w.channels == 1) {
                out[2 * i] = in[i];
                out[2 * i + 1] = in[i];
            } else {
                out[2 * i] = in[2 * i];
                out[2 * i + 1] = in[2 * i + 1];
            }
        }
        uint32_t took = (uint32_t)(now_us() - t0);
        if (us) {
            us[n++] = took;
        }
        if (took > budget_us) {
            over++;
        }
        audio_write_frames(out, nf);
        left -= (uint32_t)got;
    }
    memset(out, 0, (size_t)frames * 4);
    audio_write_frames(out, 1024);        // let the buffers drain to silence
    audio_source_resume();
    printf("done. %d of %d chunks took longer than the budget (each is an audible dropout risk)\n", over, n);
    if (us) {
        print_stats("  read+convert time per chunk", us, n);
    }
    free(us);
    free(in);
    free(out);
    fclose(f);
    return 0;
}

// ---- bank test: many clips in one big file, and random seeks into it ----
static int sd_bank(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[2], "make")) {
        if (!need(true)) {
            return 1;
        }
        int mb = argc >= 4 ? atoi(argv[3]) : 16;
        if (mb < 1 || mb > 256) {
            mb = 16;
        }
        uint8_t *b = io_buf();
        FILE *f = fopen(BANK_FILE, "wb");
        if (!f) {
            printf("cannot create %s: %s\n", BANK_FILE, strerror(errno));
            return 1;
        }
        int64_t t0 = now_us();
        for (long off = 0; off < (long)mb * 1048576L; off += IOBUF_SIZE) {
            fill_pattern(b, (uint32_t)off, IOBUF_SIZE);
            if (fwrite(b, 1, IOBUF_SIZE, f) != IOBUF_SIZE) {
                printf("write failed: %s\n", strerror(errno));
                fclose(f);
                return 1;
            }
        }
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        printf("wrote %s: %d MB in %.1f s\n", BANK_FILE, mb, (now_us() - t0) / 1e6);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[2], "seek")) {
        if (!need(false)) {
            return 1;
        }
        // sd bank seek [n] [posix]: posix = open()/lseek()/read() instead of stdio fseek()/fread()
        int n = 200;
        bool posix = false;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "posix")) {
                posix = true;
            } else if (atoi(argv[i]) > 0) {
                n = atoi(argv[i]);
            }
        }
        if (n < 10 || n > 2000) {
            n = 200;
        }
        const size_t chunk = 4096;
        FILE *f = posix ? NULL : fopen(BANK_FILE, "rb");
        int fd = posix ? open(BANK_FILE, O_RDONLY) : -1;
        if (!f && fd < 0) {
            printf("%s not found: 'sd bank make <MB>' first\n", BANK_FILE);
            return 1;
        }
        long size;
        if (posix) {
            size = (long)lseek(fd, 0, SEEK_END);
        } else {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
        }
        uint32_t *us = malloc(sizeof(uint32_t) * (size_t)n);
        uint8_t *b = io_buf();
        if (!us || size < (long)chunk * 4) {
            printf("file too small or out of memory\n");
            if (f) {
                fclose(f);
            } else {
                close(fd);
            }
            free(us);
            return 1;
        }
        printf("%s\n", posix ? "using POSIX open/lseek/read" : "using stdio fseek/fread");
        int bad = 0;
        for (int i = 0; i < n; i++) {
            long off = (long)(esp_random() % (uint32_t)(size - (long)chunk)) & ~1023L;
            int64_t t0 = now_us();
            size_t got;
            if (posix) {
                lseek(fd, off, SEEK_SET);
                ssize_t r = read(fd, b, chunk);
                got = r > 0 ? (size_t)r : 0;
            } else {
                fseek(f, off, SEEK_SET);
                got = fread(b, 1, chunk, f);
            }
            us[i] = (uint32_t)(now_us() - t0);
            for (size_t k = 0; k < got; k++) {
                if (b[k] != pat((uint32_t)off + (uint32_t)k)) {
                    bad++;
                    break;
                }
            }
        }
        if (f) {
            fclose(f);
        } else {
            close(fd);
        }
#ifdef CONFIG_FATFS_USE_FASTSEEK
        printf("CONFIG_FATFS_USE_FASTSEEK is ON\n");
#else
        printf("CONFIG_FATFS_USE_FASTSEEK is OFF\n");
#endif
        printf("random seek + 4 KB read in a %ld MB file, %d chunks with wrong data\n", size / 1048576, bad);
        print_stats("seek+read", us, n);
        printf("target: 95th percentile under about 20 ms -> %s\n",
               us[(int)((n - 1) * 0.95)] < 20000 ? "OK, the bank option is fine" : "TOO SLOW, use per-clip files or fast seek");
        free(us);
        return bad ? 1 : 0;
    }
    printf("usage: sd bank make [MB] | sd bank seek [count] [posix]\n");
    return 1;
}

// ---- scale test: hundreds of files, open by name ----
static int sd_scale(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: sd scale make <count> | open [count] | clean\n");
        return 1;
    }
    char path[300];
    if (!strcmp(argv[2], "make")) {
        if (!need(true)) {
            return 1;
        }
        int count = argc >= 4 ? atoi(argv[3]) : 500;
        if (count < 1 || count > 2000) {
            count = 500;
        }
        mkdir(SCALE_DIR, 0777);
        uint8_t *b = io_buf();
        fill_pattern(b, 0, 8192);
        int64_t t0 = now_us();
        for (int i = 0; i < count; i++) {
            snprintf(path, sizeof(path), SCALE_DIR "/clip_%04d.wav", i);
            FILE *f = fopen(path, "wb");
            if (!f || fwrite(b, 1, 8192, f) != 8192) {
                printf("failed at file %d: %s\n", i, strerror(errno));
                if (f) {
                    fclose(f);
                }
                return 1;
            }
            fclose(f);
            if ((i + 1) % 100 == 0) {
                printf("  %d files\n", i + 1);
            }
        }
        printf("made %d files of 8 KB in %.1f s (long names: needs CONFIG_FATFS_LFN_HEAP)\n", count, (now_us() - t0) / 1e6);
        return 0;
    }
    if (!strcmp(argv[2], "open")) {
        if (!need(false)) {
            return 1;
        }
        int trials = argc >= 4 ? atoi(argv[3]) : 200;
        if (trials < 10 || trials > 2000) {
            trials = 200;
        }
        DIR *d = opendir(SCALE_DIR);
        if (!d) {
            printf("no %s: run 'sd scale make <count>' first\n", SCALE_DIR);
            return 1;
        }
        int64_t t0 = now_us();
        int files = 0;
        while (readdir(d)) {
            files++;
        }
        closedir(d);
        printf("directory has %d entries, listing took %.1f ms\n", files, (now_us() - t0) / 1000.0);
        if (files < 1) {
            return 1;
        }
        uint32_t *us = malloc(sizeof(uint32_t) * (size_t)trials);
        uint8_t *b = io_buf();
        if (!us) {
            return 1;
        }
        int missing = 0, bad = 0;
        for (int i = 0; i < trials; i++) {
            int id = (int)(esp_random() % (uint32_t)files);
            snprintf(path, sizeof(path), SCALE_DIR "/clip_%04d.wav", id);
            int64_t t1 = now_us();
            FILE *f = fopen(path, "rb");
            size_t got = f ? fread(b, 1, 512, f) : 0;
            if (f) {
                fclose(f);
            }
            us[i] = (uint32_t)(now_us() - t1);
            if (!f) {
                missing++;
            } else if (got != 512 || b[0] != pat(0) || b[100] != pat(100)) {
                bad++;
            }
        }
        print_stats("open by name + read 512 bytes", us, trials);
        printf("%d not found, %d with wrong content\n", missing, bad);
        free(us);
        return missing || bad;
    }
    if (!strcmp(argv[2], "clean")) {
        if (!need(true)) {
            return 1;
        }
        DIR *d = opendir(SCALE_DIR);
        if (!d) {
            printf("nothing to clean\n");
            return 0;
        }
        int n = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            snprintf(path, sizeof(path), SCALE_DIR "/%s", e->d_name);
            if (unlink(path) == 0) {
                n++;
            }
        }
        closedir(d);
        rmdir(SCALE_DIR);
        printf("removed %d files\n", n);
        return 0;
    }
    printf("usage: sd scale make <count> | open [count] | clean\n");
    return 1;
}

static int sd_rm(int argc, char **argv)
{
    if (!need(true)) {
        return 1;
    }
    if (argc < 3) {
        printf("usage: sd rm <file>\n");
        return 1;
    }
    char path[300];
    resolve(path, sizeof(path), argv[2]);
    if (unlink(path) != 0) {
        printf("cannot remove %s: %s\n", path, strerror(errno));
        return 1;
    }
    printf("removed %s\n", path);
    return 0;
}

static int cmd_sd(int argc, char **argv)
{
    if (argc >= 2) {
        if (!strcmp(argv[1], "mount")) {
            return sd_mount(argc, argv);
        } else if (!strcmp(argv[1], "unmount")) {
            return sd_unmount();
        } else if (!strcmp(argv[1], "cd")) {
            return sd_cd();
        } else if (!strcmp(argv[1], "ls")) {
            return sd_ls(argc, argv);
        } else if (!strcmp(argv[1], "test")) {
            return sd_test();
        } else if (!strcmp(argv[1], "check")) {
            return sd_check();
        } else if (!strcmp(argv[1], "speed")) {
            return sd_speed(argc >= 3 ? atoi(argv[2]) : 4);
        } else if (!strcmp(argv[1], "gen")) {
            return sd_gen(argc, argv);
        } else if (!strcmp(argv[1], "play")) {
            return sd_play(argc, argv);
        } else if (!strcmp(argv[1], "bank")) {
            return sd_bank(argc, argv);
        } else if (!strcmp(argv[1], "scale")) {
            return sd_scale(argc, argv);
        } else if (!strcmp(argv[1], "rm")) {
            return sd_rm(argc, argv);
        }
    }
    printf("usage: sd mount [rw] [pu] [kHz] | unmount | cd | ls [dir] [max] | test | check | speed [MB] | gen <name> <rate> <s>\n"
           "       | play <name> [frames] | bank make [MB] | bank seek [n] | scale make <n> | scale open [n] | scale clean | rm <file>\n");
    return 1;
}

void register_sd_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "sd",
        .help = "microSD card (1-bit SDMMC; never formats): mount [rw] [pu] [kHz] | unmount | cd | ls | test | check | speed | gen | play | bank | scale | rm",
        .func = cmd_sd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
