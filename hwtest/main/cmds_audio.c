// Stage 6: I2S output to the codec and a sine tone on the headphone jack.
//
// The ESP32 is the I2S master and supplies MCLK on GPIO0 (the codec is in slave mode). Pins are the older-module
// map: BCLK 27, WS 25, data out 26 (to be confirmed by hearing the tone).
//
// Level: the sine peaks at -1 dBFS by default (`tone <Hz> <dBFS>` changes it). `vol` sets the net gain in dB (+4.5 = loudest; the main firmware's earpiece
// range is -43.5 to +4.5). To keep the noise floor down the attenuation is done in the analog output stage first
// (registers 0x2e-0x31, 1.5 dB steps from +4.5 dB at 0x21 down to -45 dB at 0x00), which attenuates the codec's own
// noise along with the signal. The DAC digital volume (0x1a/0x1b, 0.5 dB steps) only takes the remainder.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "esp_console.h"
#include "cmds.h"

#define I2S_PORT      I2S_NUM_0
#define MCLK_PIN      GPIO_NUM_0
#define BUF_FRAMES    256
#define DEFAULT_TONE_DBFS (-1.0f)   // near full scale: the biggest clean signal for the best signal-to-noise
#define DEFAULT_GAIN  (-40.0f)      // quiet first, so the first tone does not blast the headphones
#define GAIN_MAX_DB   4.5f
#define GAIN_MIN_DB   (-91.5f)

#define REG_DACPOWER    0x04
#define REG_DACCONTROL3 0x19        // bit 2 = DAC mute
#define REG_LDACVOL     0x1a
#define REG_RDACVOL     0x1b
#define REG_CHIPPOWER   0x02
#define REG_CONTROL1    0x00
#define REG_CONTROL2    0x01

static int pin_bck = 27, pin_ws = 25, pin_dout = 26, pin_din = 35;
static bool i2s_installed;
static bool audio_on;
static volatile bool task_run;
static volatile bool task_alive;
static volatile float tone_hz;      // 0 = silence
static volatile float tone_amp = 0.891f;   // -1 dBFS
static int sample_rate = 8000;
static float gain_db = DEFAULT_GAIN;

#define ANALOG_MAX_REG  0x21      // +4.5 dB
#define ANALOG_STEP_DB  1.5f
#define REG_OUTVOL_FIRST 0x2e     // LOUT1, ROUT1, LOUT2, ROUT2 volumes: 0x2e to 0x31

typedef struct {
    int analog;    // registers 0x2e-0x31
    int digital;   // registers 0x1a, 0x1b
} gain_regs_t;

static gain_regs_t gain_to_regs(float g)
{
    if (g > GAIN_MAX_DB) {
        g = GAIN_MAX_DB;
    }
    if (g < GAIN_MIN_DB) {
        g = GAIN_MIN_DB;
    }
    float att = GAIN_MAX_DB - g;                               // total attenuation from the loudest setting
    int steps = (int)floorf(att / ANALOG_STEP_DB);
    if (steps > ANALOG_MAX_REG) {
        steps = ANALOG_MAX_REG;                                 // analog stage is at -45 dB; digital takes the rest
    }
    gain_regs_t r;
    r.analog = ANALOG_MAX_REG - steps;
    r.digital = (int)lroundf(2.0f * (att - ANALOG_STEP_DB * (float)steps));   // 0.5 dB per step
    if (r.digital > 192) {
        r.digital = 192;
    }
    return r;
}

static esp_err_t apply_gain(void)
{
    gain_regs_t r = gain_to_regs(gain_db);
    esp_err_t err = ESP_OK;
    for (int reg = REG_OUTVOL_FIRST; reg < REG_OUTVOL_FIRST + 4 && err == ESP_OK; reg++) {
        err = codec_write((uint8_t)reg, (uint8_t)r.analog);
    }
    if (err == ESP_OK) {
        err = codec_write(REG_LDACVOL, (uint8_t)r.digital);
    }
    if (err == ESP_OK) {
        err = codec_write(REG_RDACVOL, (uint8_t)r.digital);
    }
    return err;
}

// ---- Equalizer: one biquad filter on the output, to tame the high frequencies of a small earpiece ----
typedef enum { EQ_OFF, EQ_LP, EQ_HP, EQ_HS } eq_type_t;
typedef struct {
    float b0, b1, b2, a1, a2;   // normalized: a0 = 1
} biquad_t;

static volatile eq_type_t eq_type = EQ_OFF;
static volatile float eq_fc;
static volatile float eq_db;
static volatile uint32_t eq_gen;    // bumped when the filter setting changes

// RBJ audio-EQ-cookbook formulas. EQ_LP / EQ_HP: 2nd-order Butterworth low-pass / high-pass. EQ_HS: high shelf, slope 1.
static biquad_t eq_design(eq_type_t t, float fc, float db, float fs)
{
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float c = cosf(w0), sn = sinf(w0);
    float b0, b1, b2, a0, a1, a2;
    if (t == EQ_LP || t == EQ_HP) {
        float alpha = sn / (2.0f * 0.70710678f);
        if (t == EQ_LP) {
            b0 = (1.0f - c) / 2.0f;
            b1 = 1.0f - c;
        } else {
            b0 = (1.0f + c) / 2.0f;
            b1 = -(1.0f + c);
        }
        b2 = b0;
        a0 = 1.0f + alpha;
        a1 = -2.0f * c;
        a2 = 1.0f - alpha;
    } else {
        float A = powf(10.0f, db / 40.0f);
        float alpha = sn / 2.0f * sqrtf(2.0f);
        float sq = 2.0f * sqrtf(A) * alpha;
        b0 = A * ((A + 1.0f) + (A - 1.0f) * c + sq);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * c - sq);
        a0 = (A + 1.0f) - (A - 1.0f) * c + sq;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * c);
        a2 = (A + 1.0f) - (A - 1.0f) * c - sq;
    }
    biquad_t q = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return q;
}

static float biquad_db(const biquad_t *q, float f, float fs)
{
    float w = 2.0f * (float)M_PI * f / fs;
    float nr = q->b0 + q->b1 * cosf(w) + q->b2 * cosf(2 * w);
    float ni = -(q->b1 * sinf(w) + q->b2 * sinf(2 * w));
    float dr = 1.0f + q->a1 * cosf(w) + q->a2 * cosf(2 * w);
    float di = -(q->a1 * sinf(w) + q->a2 * sinf(2 * w));
    return 10.0f * log10f((nr * nr + ni * ni) / (dr * dr + di * di));
}

static void audio_task(void *arg)
{
    static int16_t buf[BUF_FRAMES * 2];
    float phase = 0.0f;
    biquad_t q = {1, 0, 0, 0, 0};
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    uint32_t gen_seen = ~0u;
    int rate_seen = 0;
    task_alive = true;
    while (task_run) {
        float hz = tone_hz;
        float inc = 2.0f * (float)M_PI * hz / (float)sample_rate;
        if (gen_seen != eq_gen || rate_seen != sample_rate) {
            gen_seen = eq_gen;
            rate_seen = sample_rate;
            if (eq_type != EQ_OFF) {
                q = eq_design(eq_type, eq_fc, eq_db, (float)sample_rate);
            }
        }
        bool eq_on = eq_type != EQ_OFF;
        for (int i = 0; i < BUF_FRAMES; i++) {
            float x = 0.0f;
            if (hz > 0.0f) {
                x = sinf(phase) * tone_amp;
                phase += inc;
                if (phase > 2.0f * (float)M_PI) {
                    phase -= 2.0f * (float)M_PI;
                }
            }
            if (eq_on) {
                float y = q.b0 * x + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                x = y;
            }
            if (x > 1.0f) {
                x = 1.0f;
            } else if (x < -1.0f) {
                x = -1.0f;
            }
            int16_t s = (int16_t)(x * 32767.0f);
            buf[2 * i] = s;
            buf[2 * i + 1] = s;
        }
        size_t written;
        i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
    }
    task_alive = false;
    vTaskDelete(NULL);
}

static void stop_task(void)
{
    task_run = false;
    while (task_alive) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void start_task(void)
{
    task_run = true;
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);
    while (!task_alive) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static esp_err_t i2s_setup(void)
{
    if (i2s_installed) {
        return ESP_OK;
    }
    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUF_FRAMES,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
    i2s_pin_config_t pins = {
        .mck_io_num = MCLK_PIN,
        .bck_io_num = pin_bck,
        .ws_io_num = pin_ws,
        .data_out_num = pin_dout,
        .data_in_num = pin_din,
    };
    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err == ESP_OK) {
        err = i2s_set_pin(I2S_PORT, &pins);
    }
    if (err != ESP_OK) {
        return err;
    }
    i2s_installed = true;
    return ESP_OK;
}

// DAC power register 0x04 bit map: 5 LOUT1, 4 ROUT1, 3 LOUT2, 2 ROUT2. The repo's constants (LOUT2|ROUT2 = 0x28)
// do not match it and turned on only the left channel. LOUT2/ROUT2 is the headphone jack; LOUT1/ROUT1 feed the two
// class-D speaker amplifiers (J3, J4), which also need IO21 high.
typedef enum { OUT_HP, OUT_SPK, OUT_BOTH } out_sel_t;
static out_sel_t out_sel = OUT_HP;
#define AMP_ENABLE_PIN 21

static const char *out_name(void)
{
    return out_sel == OUT_HP ? "headphone jack" : out_sel == OUT_SPK ? "speaker outputs (J3/J4)" : "headphone jack + speaker outputs";
}

static int apply_output(void)
{
    uint8_t power = out_sel == OUT_HP ? 0x0c : out_sel == OUT_SPK ? 0x30 : 0x3c;
    int err = codec_write(REG_DACPOWER, power) != ESP_OK;
    if (out_sel == OUT_HP) {
        gpio_tools_release(AMP_ENABLE_PIN);      // R51 pulls the amp enable low
    } else {
        vTaskDelay(pdMS_TO_TICKS(50));           // let the codec output settle before the amps wake
        err |= !gpio_tools_drive(AMP_ENABLE_PIN, 1);
    }
    return err;
}

// Same steps as es8388_start(): reset the codec state machine, set the reference, run, then power the DAC and
// unmute.
static int codec_start(void)
{
    int err = 0;
    err |= codec_write(REG_CHIPPOWER, 0xf0) != ESP_OK;
    err |= codec_write(REG_CONTROL1, 0x16) != ESP_OK;
    err |= codec_write(REG_CONTROL2, 0x50) != ESP_OK;
    err |= codec_write(REG_CHIPPOWER, 0x00) != ESP_OK;
    err |= codec_write(REG_DACPOWER, 0x0c) != ESP_OK;   // headphone outputs first; the speaker amps come last
    err |= apply_gain() != ESP_OK;
    err |= codec_write(REG_DACCONTROL3, 0x60) != ESP_OK;   // unmute
    err |= apply_output();
    mic_apply();                                            // ADC input setup (line-in, left channel only)
    return err;
}

bool audio_is_on(void)
{
    return audio_on;
}

int audio_rate(void)
{
    return sample_rate;
}

int audio_read_frames(int16_t *buf, int frames, int timeout_ms)
{
    if (!audio_on) {
        return -1;
    }
    size_t got = 0;
    if (i2s_read(I2S_PORT, buf, (size_t)frames * 4, &got, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        return -1;
    }
    return (int)(got / 4);
}

static int audio_start(int rate)
{
    if (!codec_ready()) {
        return 1;
    }
    if (audio_on) {
        stop_task();
    }
    sample_rate = rate;
    esp_err_t err = i2s_setup();
    if (err != ESP_OK) {
        printf("I2S setup failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    if (!audio_on) {
        i2s_start(I2S_PORT);
    }
    i2s_set_sample_rates(I2S_PORT, rate);
    start_task();               // clocks (including MCLK) run before the codec is programmed
    vTaskDelay(pdMS_TO_TICKS(20));
    if (codec_init() != 0 || codec_start() != 0) {
        printf("codec setup failed\n");
        return 1;
    }
    audio_on = true;
    printf("audio on: %d Hz, I2S BCLK %d / WS %d / DOUT %d, MCLK on GPIO0, gain %.1f dB, tone %s, output: %s\n", rate,
           pin_bck, pin_ws, pin_dout, gain_db, tone_hz > 0 ? "on" : "off (silence)", out_name());
    return 0;
}

static int audio_stop(void)
{
    if (!audio_on) {
        printf("audio is already off\n");
        return 0;
    }
    gpio_tools_release(AMP_ENABLE_PIN);   // amps off first
    codec_write(REG_DACCONTROL3, 0x64);   // mute
    stop_task();
    codec_write(REG_DACPOWER, 0xc0);      // DAC and outputs off
    i2s_stop(I2S_PORT);
    audio_on = false;
    printf("audio off\n");
    return 0;
}

static int cmd_audio(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "on")) {
        int rate = argc >= 3 ? atoi(argv[2]) : sample_rate;
        if (rate != 8000 && rate != 16000 && rate != 22050 && rate != 44100 && rate != 11025 && rate != 32000 &&
            rate != 48000) {
            printf("rate must be one of 8000 11025 16000 22050 32000 44100 48000\n");
            return 1;
        }
        return audio_start(rate);
    } else if (argc >= 2 && !strcmp(argv[1], "off")) {
        return audio_stop();
    } else if (argc >= 5 && !strcmp(argv[1], "pins")) {
        if (i2s_installed) {
            printf("pins can only be changed before the first 'audio on' (reset the board)\n");
            return 1;
        }
        int p[3] = {atoi(argv[2]), atoi(argv[3]), atoi(argv[4])};
        for (int i = 0; i < 3; i++) {
            if (p[i] != 5 && p[i] != 25 && p[i] != 26 && p[i] != 27) {
                printf("allowed pins: 5, 25, 26, 27\n");
                return 1;
            }
        }
        pin_bck = p[0];
        pin_ws = p[1];
        pin_dout = p[2];
        printf("I2S BCLK %d / WS %d / DOUT %d\n", pin_bck, pin_ws, pin_dout);
        return 0;
    } else if (argc >= 3 && !strcmp(argv[1], "out")) {
        if (!strcmp(argv[2], "hp")) {
            out_sel = OUT_HP;
        } else if (!strcmp(argv[2], "spk")) {
            out_sel = OUT_SPK;
        } else if (!strcmp(argv[2], "both")) {
            out_sel = OUT_BOTH;
        } else {
            printf("usage: audio out hp|spk|both\n");
            return 1;
        }
        if (audio_on && apply_output() != 0) {
            printf("output change failed\n");
            return 1;
        }
        printf("output: %s%s\n", out_name(), audio_on ? "" : " (applied at 'audio on')");
        return 0;
    } else if (argc >= 2 && !strcmp(argv[1], "status")) {
        gain_regs_t r = gain_to_regs(gain_db);
        printf("audio %s, %d Hz, tone %.0f Hz, gain %.1f dB (analog reg %d, DAC reg %d), output: %s, BCLK %d / WS %d / DOUT %d\n",
               audio_on ? "on" : "off", sample_rate, tone_hz, gain_db, r.analog, r.digital, out_name(), pin_bck,
               pin_ws, pin_dout);
        return 0;
    }
    printf("usage: audio on [rate] | off | status | out hp|spk|both | pins <bck> <ws> <dout>\n");
    return 1;
}

static int cmd_tone(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: tone <Hz> [dBFS, -60 to 0; default %.0f] | off\n", DEFAULT_TONE_DBFS);
        return 1;
    }
    if (!strcmp(argv[1], "off")) {
        tone_hz = 0.0f;
        printf("tone off (silence)\n");
        return 0;
    }
    float hz = (float)atof(argv[1]);
    if (hz < 20.0f || hz > sample_rate / 2.0f - 100.0f) {
        printf("frequency must be 20 Hz to %d Hz at %d Hz sampling\n", sample_rate / 2 - 100, sample_rate);
        return 1;
    }
    float dbfs = argc >= 3 ? (float)atof(argv[2]) : DEFAULT_TONE_DBFS;
    if (dbfs < -60.0f || dbfs > 0.0f) {
        printf("level must be -60 to 0 dBFS\n");
        return 1;
    }
    tone_amp = powf(10.0f, dbfs / 20.0f);
    tone_hz = hz;
    printf("tone %.0f Hz at %.1f dBFS%s\n", hz, dbfs, audio_on ? "" : " (run 'audio on' to hear it)");
    return 0;
}

static int cmd_vol(int argc, char **argv)
{
    if (argc < 2) {
        gain_regs_t r0 = gain_to_regs(gain_db);
        printf("gain %.1f dB (analog reg %d, DAC reg %d). usage: vol <dB, %.1f to %.1f>\n", gain_db, r0.analog,
               r0.digital, GAIN_MIN_DB, GAIN_MAX_DB);
        return 0;
    }
    float g = (float)atof(argv[1]);
    if (g < GAIN_MIN_DB || g > GAIN_MAX_DB) {
        printf("gain must be %.1f to %.1f dB\n", GAIN_MIN_DB, GAIN_MAX_DB);
        return 1;
    }
    gain_db = g;
    if (audio_on && apply_gain() != ESP_OK) {
        printf("codec write failed\n");
        return 1;
    }
    gain_regs_t r = gain_to_regs(gain_db);
    printf("gain %.1f dB (analog reg %d, DAC reg %d)%s\n", gain_db, r.analog, r.digital,
           audio_on ? "" : ", applied at 'audio on'");
    return 0;
}

// The 10-step volume model from doc/initial_design.md: digit d -> k = d (0 -> 10),
// gain = min + (k / 11) * (max - min). Defaults to the earpiece range.
static int cmd_volstep(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: volstep <digit 0-9> [min_dB max_dB]   (digit 1 quietest, 0 loudest; default -43.5 to +4.5)\n");
        return 1;
    }
    int d = atoi(argv[1]);
    if (d < 0 || d > 9 || (argv[1][0] < '0' || argv[1][0] > '9')) {
        printf("digit must be 0 to 9\n");
        return 1;
    }
    float lo = -43.5f, hi = 4.5f;
    if (argc >= 4) {
        lo = (float)atof(argv[2]);
        hi = (float)atof(argv[3]);
    }
    int k = d == 0 ? 10 : d;
    float g = lo + ((float)k / 11.0f) * (hi - lo);
    char gs[16];
    snprintf(gs, sizeof(gs), "%.2f", g);
    char *args[] = {"vol", gs};
    printf("digit %d -> step %d of 11 -> %.2f dB\n", d, k, g);
    return cmd_vol(2, args);
}

static int cmd_eq(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "off")) {
        eq_type = EQ_OFF;
        eq_gen++;
        printf("eq off\n");
        return 0;
    }
    if (argc >= 3 && (!strcmp(argv[1], "lp") || !strcmp(argv[1], "hp"))) {
        float fc = (float)atof(argv[2]);
        if (fc < 20.0f || fc > sample_rate / 2.0f - 200.0f) {
            printf("cutoff must be 20 Hz to %d Hz at %d Hz sampling\n", sample_rate / 2 - 200, sample_rate);
            return 1;
        }
        eq_fc = fc;
        eq_db = 0.0f;
        eq_type = !strcmp(argv[1], "lp") ? EQ_LP : EQ_HP;
    } else if (argc >= 4 && !strcmp(argv[1], "hs")) {
        float fc = (float)atof(argv[2]);
        float db = (float)atof(argv[3]);
        if (fc < 100.0f || fc > sample_rate / 2.0f - 200.0f || db < -24.0f || db > 12.0f) {
            printf("corner 100 Hz to %d Hz, gain -24 to +12 dB\n", sample_rate / 2 - 200);
            return 1;
        }
        eq_fc = fc;
        eq_db = db;
        eq_type = EQ_HS;
    } else {
        printf("usage: eq lp <Hz> | hp <Hz> | hs <Hz> <dB> | off\n"
               "  lp / hp = 2nd-order low-pass / high-pass at <Hz>; hs = high shelf: <dB> (negative cuts) above <Hz>\n");
        return 1;
    }
    eq_gen++;
    biquad_t q = eq_design(eq_type, eq_fc, eq_db, (float)sample_rate);
    static const float freqs[] = {60, 100, 300, 500, 1000, 2000, 3000, 3400};
    printf("eq on at %d Hz sampling; response:", sample_rate);
    for (size_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
        printf(" %.0f Hz %+.1f dB%s", freqs[i], biquad_db(&q, freqs[i], (float)sample_rate),
               i + 1 < sizeof(freqs) / sizeof(freqs[0]) ? "," : "");
    }
    printf("\n");
    return 0;
}

static int cmd_mute(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
        printf("usage: mute on|off\n");
        return 1;
    }
    if (codec_write(REG_DACCONTROL3, !strcmp(argv[1], "on") ? 0x64 : 0x60) != ESP_OK) {
        printf("codec write failed\n");
        return 1;
    }
    printf("DAC mute %s\n", argv[1]);
    return 0;
}

void register_audio_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "audio", .help = "Codec audio out: audio on [rate] | off | status | out hp|spk|both | pins <bck> <ws> <dout>", .func = cmd_audio},
        {.command = "tone", .help = "Sine tone: tone <Hz> [dBFS, default -1] | off (silence)", .func = cmd_tone},
        {.command = "vol", .help = "Net output gain in dB, -91.5 to +4.5: vol <dB>", .func = cmd_vol},
        {.command = "volstep", .help = "10-step volume: volstep <digit 0-9> [min_dB max_dB]", .func = cmd_volstep},
        {.command = "mute", .help = "DAC mute: mute on|off", .func = cmd_mute},
        {.command = "eq", .help = "Output filter: eq lp <Hz> | hp <Hz> | hs <Hz> <dB> | off", .func = cmd_eq},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
