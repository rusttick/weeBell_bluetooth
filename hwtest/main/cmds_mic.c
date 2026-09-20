// Stage 7: microphone (line-in) input. The MAX9814 module goes into the line-in jack (LIN2 / RIN2). The board's two
// onboard microphones are wired to the SAME codec inputs (through C18 and C20), so they cannot be excluded in
// firmware: remove C18 and C20. The firmware's part is to use only the left ADC channel, keep the right channel
// (nothing connected, or unused) out of the audio, and keep the gain stages sensible.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "cmds.h"

#define REG_ADCPOWER    0x03
#define REG_ADCCONTROL1 0x09    // analog input amplifier (PGA): MicAmpL[7:4], MicAmpR[3:0], 3 dB per step, 0 to 8
#define REG_ADCCONTROL2 0x0a    // input select: LINSEL[7:6], RINSEL[5:4]; 01 = LIN2/RIN2
#define REG_ADCCONTROL4 0x0c    // DATSEL[7:6], word length, format
#define REG_ADCCONTROL8 0x10    // ADC digital volume, left (0 dB = 0, 0.5 dB per step down)
#define REG_ADCCONTROL9 0x11    // ADC digital volume, right
#define REG_ADCCONTROL14 0x16   // noise gate

typedef enum { CH_LEFT, CH_BOTH } ch_mode_t;

// Defaults chosen for the best signal-to-noise from the MAX9814: left channel only, no analog gain after it.
static ch_mode_t ch_mode = CH_LEFT;
static int pga_step = 0;        // 0 dB
static float adc_db = 0.0f;     // digital volume
static bool gate_on = false;

void mic_apply(void)
{
    codec_write(REG_ADCCONTROL2, 0x50);                                   // LIN2 / RIN2: the line-in jack
    codec_write(REG_ADCCONTROL1, (uint8_t)((pga_step << 4) | pga_step));
    // ADC power: bit 7 AINL, 6 AINR, 5 ADCL, 4 ADCR (1 = off), bit 3 MICBIAS (1 = off, we use our own supply), bit 0 low power.
    // Left only: switch the right input and the right ADC off, so its noise never reaches the I2S data.
    codec_write(REG_ADCPOWER, ch_mode == CH_LEFT ? 0x59 : 0x09);
    // DATSEL = 01: the left ADC data goes to both I2S slots. 00 = each ADC to its own slot.
    codec_write(REG_ADCCONTROL4, ch_mode == CH_LEFT ? 0x4c : 0x0c);
    int v = (int)lroundf(-adc_db * 2.0f);
    if (v < 0) {
        v = 0;
    }
    if (v > 192) {
        v = 192;
    }
    codec_write(REG_ADCCONTROL8, (uint8_t)v);
    codec_write(REG_ADCCONTROL9, (uint8_t)v);
    codec_write(REG_ADCCONTROL14, gate_on ? 0xdb : 0xda);                 // noise gate on / off
}

static float to_db(double x)
{
    return x <= 1e-9 ? -99.9f : (float)(20.0 * log10(x));
}

// Read audio for a number of seconds and print the level of each channel every half second.
static int mic_level(int seconds)
{
    if (!audio_is_on()) {
        printf("run 'audio on 8000' first (I2S and the codec clock must be running)\n");
        return 1;
    }
    const int rate = audio_rate();
    const int block = rate / 2;
    static int16_t buf[512 * 2];
    printf("levels in dBFS every 0.5 s (peak / rms / dc as a fraction of full scale), %d Hz:\n", rate);
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (esp_timer_get_time() < end) {
        double ss[2] = {0, 0}, dc[2] = {0, 0};
        int pk[2] = {0, 0};
        int n = 0;
        while (n < block) {
            int want = block - n > 512 ? 512 : block - n;
            int got = audio_read_frames(buf, want, 500);
            if (got <= 0) {
                printf("no audio data from the codec (I2S read failed)\n");
                return 1;
            }
            for (int i = 0; i < got; i++) {
                for (int c = 0; c < 2; c++) {
                    int v = buf[2 * i + c];
                    ss[c] += (double)v * v;
                    dc[c] += v;
                    if (abs(v) > pk[c]) {
                        pk[c] = abs(v);
                    }
                }
            }
            n += got;
        }
        printf("L peak %6.1f rms %6.1f dc %+.4f | R peak %6.1f rms %6.1f dc %+.4f\n",
               to_db(pk[0] / 32768.0), to_db(sqrt(ss[0] / n) / 32768.0), dc[0] / n / 32768.0,
               to_db(pk[1] / 32768.0), to_db(sqrt(ss[1] / n) / 32768.0), dc[1] / n / 32768.0);
    }
    return 0;
}

// ---- Averaged levels: `mic avg` ----
// Average over a number of seconds; also says whether the right channel is a copy of the left (chan left) or not.
static int mic_avg(int seconds)
{
    if (!audio_is_on()) {
        printf("run 'audio on 8000' first (I2S and the codec clock must be running)\n");
        return 1;
    }
    static int16_t buf[512 * 2];
    const int rate = audio_rate();
    long total = (long)seconds * rate, n = 0, same = 0;
    double ss[2] = {0, 0}, dc[2] = {0, 0};
    int pk[2] = {0, 0};
    audio_read_frames(buf, 512, 200);                                    // discard stale data
    printf("measuring %d s, left and right channel...\n", seconds);
    while (n < total) {
        int got = audio_read_frames(buf, 512, 500);
        if (got <= 0) {
            printf("no audio data from the codec (I2S read failed)\n");
            return 1;
        }
        for (int i = 0; i < got; i++) {
            for (int c = 0; c < 2; c++) {
                int v = buf[2 * i + c];
                ss[c] += (double)v * v;
                dc[c] += v;
                if (abs(v) > pk[c]) {
                    pk[c] = abs(v);
                }
            }
            if (buf[2 * i] == buf[2 * i + 1]) {
                same++;
            }
        }
        n += got;
    }
    printf("left : rms %6.1f dBFS, peak %6.1f dBFS, dc %+.4f\n", to_db(sqrt(ss[0] / n) / 32768.0), to_db(pk[0] / 32768.0),
           dc[0] / n / 32768.0);
    printf("right: rms %6.1f dBFS, peak %6.1f dBFS, dc %+.4f\n", to_db(sqrt(ss[1] / n) / 32768.0), to_db(pk[1] / 32768.0),
           dc[1] / n / 32768.0);
    printf("right equals left in %.1f%% of the samples%s\n", 100.0 * same / n,
           same == n ? " (right is an exact copy of the left channel)" : "");
    return 0;
}

// ---- Signal-to-noise test: `mic snr [label]` ----
// Phase 1: silence. Phase 2: speech. Phase 3: silence again. Uses the LEFT channel, in 100 ms blocks.
#define SNR_MAX_BLOCKS 200
typedef struct {
    double rms;      // linear, full scale = 1
    int peak;
    int clips;
    int nblk;
} phase_t;

static float blk_rms[3][SNR_MAX_BLOCKS];

static bool phase_run(int seconds, float *blk, phase_t *out)
{
    static int16_t buf[512 * 2];
    const int blkframes = audio_rate() / 10;
    double ss = 0;
    long n = 0;
    int peak = 0, clips = 0, nb = 0;
    audio_read_frames(buf, 512, 100);                                    // discard stale data
    for (int b = 0; b < seconds * 10 && nb < SNR_MAX_BLOCKS; b++) {
        double bss = 0;
        int got = 0;
        while (got < blkframes) {
            int want = blkframes - got > 512 ? 512 : blkframes - got;
            int r = audio_read_frames(buf, want, 500);
            if (r <= 0) {
                return false;
            }
            for (int i = 0; i < r; i++) {
                int v = buf[2 * i];
                bss += (double)v * v;
                if (abs(v) > peak) {
                    peak = abs(v);
                }
                if (abs(v) >= 32700) {
                    clips++;
                }
            }
            got += r;
        }
        blk[nb++] = (float)(sqrt(bss / got) / 32768.0);
        ss += bss;
        n += got;
    }
    out->rms = sqrt(ss / n) / 32768.0;
    out->peak = peak;
    out->clips = clips;
    out->nblk = nb;
    return true;
}

static void countdown(const char *what)
{
    printf("%s in 3", what);
    for (int i = 2; i >= 1; i--) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf(", %d", i);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf(" ...\n");
}

static int mic_snr(const char *label)
{
    if (!audio_is_on()) {
        printf("run 'audio on 8000' first (I2S and the codec clock must be running)\n");
        return 1;
    }
    phase_t q1, sp, q2;
    printf("Signal-to-noise test on the LEFT channel. Hold the handset or the module as in use.\n");
    countdown("Stay SILENT for 5 seconds, starting");
    if (!phase_run(5, blk_rms[0], &q1)) {
        printf("no audio data from the codec\n");
        return 1;
    }
    countdown("Then SPEAK a steady phrase (count \"one two three ...\" at a normal level) for 5 seconds, starting");
    if (!phase_run(5, blk_rms[1], &sp)) {
        printf("no audio data from the codec\n");
        return 1;
    }
    countdown("Then stay SILENT again for 5 seconds, starting");
    if (!phase_run(5, blk_rms[2], &q2)) {
        printf("no audio data from the codec\n");
        return 1;
    }

    // Speech level: RMS over the 100 ms blocks that are at least 10 dB above the first silence.
    double thr = q1.rms * 3.162;
    double act = 0;
    int nact = 0;
    for (int i = 0; i < sp.nblk; i++) {
        if (blk_rms[1][i] > thr) {
            act += (double)blk_rms[1][i] * blk_rms[1][i];
            nact++;
        }
    }
    double speech = nact ? sqrt(act / nact) : 0.0;
    float sil1 = to_db(q1.rms), sil2 = to_db(q2.rms), spe = to_db(speech), pk = to_db(sp.peak / 32768.0);

    printf("\nsilence before: rms %6.1f dBFS\n", sil1);
    printf("silence after : rms %6.1f dBFS   (%+.1f dB versus before: a rise here is the AGC boosting room noise)\n", sil2,
           sil2 - sil1);
    if (nact < 3) {
        printf("speech        : NO VOICE DETECTED (fewer than 0.3 s more than 10 dB above the silence)\n");
        printf("[%s] silence %.1f / %.1f dBFS, no voice detected\n", label, sil1, sil2);
        return 0;
    }
    printf("speech        : rms %6.1f dBFS, peak %6.1f dBFS, %d clipped samples, %d of %d blocks active\n", spe, pk,
           sp.clips, nact, sp.nblk);
    float snr1 = spe - sil1, snr2 = spe - sil2;
    printf("signal-to-noise: %.1f dB (against the first silence), %.1f dB (against the second)\n", snr1, snr2);
    printf("checks: voice detected; speech peak %s -6 dBFS (%s); clipping %s; signal-to-noise %s 40 dB (%s)\n",
           pk <= -6.0f ? "at or below" : "ABOVE", pk <= -6.0f ? "ok" : "too loud", sp.clips ? "YES" : "none",
           (snr1 < snr2 ? snr1 : snr2) >= 40.0f ? "at least" : "BELOW", (snr1 < snr2 ? snr1 : snr2) >= 40.0f ? "ok" : "low");
    printf("[%s] silence %.1f / %.1f, speech rms %.1f, peak %.1f, S/N %.1f dB\n", label, sil1, sil2, spe, pk,
           snr1 < snr2 ? snr1 : snr2);
    return 0;
}

static int cmd_mic(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "level")) {
        return mic_level(argc >= 3 ? atoi(argv[2]) : 5);
    } else if (argc >= 2 && !strcmp(argv[1], "avg")) {
        int secs = argc >= 3 ? atoi(argv[2]) : 5;
        return mic_avg(secs < 1 ? 1 : secs > 120 ? 120 : secs);
    } else if (argc >= 2 && !strcmp(argv[1], "snr")) {
        return mic_snr(argc >= 3 ? argv[2] : "test");
    } else if (argc >= 3 && !strcmp(argv[1], "chan")) {
        if (!strcmp(argv[2], "left")) {
            ch_mode = CH_LEFT;
        } else if (!strcmp(argv[2], "both")) {
            ch_mode = CH_BOTH;
        } else {
            printf("usage: mic chan left|both\n");
            return 1;
        }
    } else if (argc >= 3 && !strcmp(argv[1], "pga")) {
        int n = atoi(argv[2]);
        if (n < 0 || n > 8) {
            printf("pga step must be 0 to 8 (0 to 24 dB in 3 dB steps)\n");
            return 1;
        }
        pga_step = n;
    } else if (argc >= 3 && !strcmp(argv[1], "gain")) {
        float g = (float)atof(argv[2]);
        if (g > 0.0f || g < -96.0f) {
            printf("ADC digital gain must be -96 to 0 dB\n");
            return 1;
        }
        adc_db = g;
    } else if (argc >= 3 && !strcmp(argv[1], "gate")) {
        if (strcmp(argv[2], "on") && strcmp(argv[2], "off")) {
            printf("usage: mic gate on|off\n");
            return 1;
        }
        gate_on = !strcmp(argv[2], "on");
    } else if (argc >= 2 && !strcmp(argv[1], "status")) {
        // fall through to the print below
    } else {
        printf("usage: mic level [s] | avg [s] | snr [label] | chan left|both | pga <0-8> | gain <dB> | gate on|off | status\n");
        return 1;
    }
    if (audio_is_on()) {
        mic_apply();
    }
    printf("mic: input line-in (LIN2/RIN2), channel %s, analog gain %d dB (step %d), digital %.1f dB, noise gate %s%s\n",
           ch_mode == CH_LEFT ? "left only (right ADC off, left on both slots)" : "both", pga_step * 3, pga_step,
           adc_db, gate_on ? "on" : "off", audio_is_on() ? "" : " (applied at 'audio on')");
    return 0;
}

void register_mic_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "mic",
        .help = "Line-in input (needs 'audio on'): level [s] | avg [s] | snr [label] | chan left|both | pga <0-8> | gain <dB> | gate on|off | status",
        .func = cmd_mic,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
