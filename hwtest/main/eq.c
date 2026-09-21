// Equalizer profiles and the biquad engine. See eq.h.
//
// MICROPHONE profiles anticipate what a plastic handset housing does to a capsule behind a few small holes:
//   - the high end falls away above roughly 3 kHz (small holes, cloth or foam, a wall in front of the capsule);
//   - the mouthpiece cup and holes resonate somewhere between 1.5 and 3 kHz, which makes the voice honky;
//   - close talking and a sealed cavity boost the bass (proximity effect, boominess), and handling adds rumble below 200 Hz;
//   - a passive high-pass (0.047 to 0.1 uF into about 10 kOhm) may already be fitted, so these high-pass corners are
//     modest: they add to it.
// EARPIECE profiles anticipate a small 4 ohm receiver: little output below 300 Hz (worse if it does not seal to the ear), a
// harsh peak between 2 and 3 kHz, a fall-off above about 4 kHz, and a fixed hiss from the codec and amplifier that a high
// boost would raise.
// These are starting points to be judged by ear and by recordings, not final values.
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "xtensa/hal.h"
#include "sdkconfig.h"
#include "eq.h"

// ---- Profiles. Section: {kind, frequency Hz, Q, gain dB} ----
const eq_profile_t eq_mic_profiles[EQ_PROFILES] = {
    {"flat", "no equalization", 0.0f, 0, {{0}}},
    {"light lift", "mild high-frequency loss: 120 Hz high-pass, +3 dB above 3 kHz", 0.0f, 2,
     {{EQK_HP, 120, 0, 0}, {EQK_HSHELF, 3000, 0, 3}}},
    {"housing loss", "typical loss behind small holes: 150 Hz high-pass, +6 dB above 2.8 kHz", -1.0f, 2,
     {{EQK_HP, 150, 0, 0}, {EQK_HSHELF, 2800, 0, 6}}},
    {"housing loss strong", "muffled through a thick wall or foam: 180 Hz high-pass, +9 dB above 2.5 kHz, +2 dB at 3.6 kHz",
     -3.0f, 3, {{EQK_HP, 180, 0, 0}, {EQK_HSHELF, 2500, 0, 9}, {EQK_PEAK, 3600, 1.0f, 2}}},
    {"tame cavity", "honky cavity resonance: -6 dB at 2 kHz (narrow), +3 dB above 3.5 kHz, 150 Hz high-pass", 0.0f, 3,
     {{EQK_HP, 150, 0, 0}, {EQK_PEAK, 2000, 2.5f, -6}, {EQK_HSHELF, 3500, 0, 3}}},
    {"reduce boom", "boomy from close talking or a sealed cavity: 250 Hz high-pass, -4 dB below 400 Hz", 0.0f, 2,
     {{EQK_HP, 250, 0, 0}, {EQK_LSHELF, 400, 0, -4}}},
    {"telephone band", "300 Hz to 3.4 kHz only (noise and rumble out)", 0.0f, 2,
     {{EQK_HP, 300, 0, 0}, {EQK_LP, 3400, 0, 0}}},
    {"presence", "voice a little forward: 180 Hz high-pass, +5 dB at 3 kHz", -2.0f, 2,
     {{EQK_HP, 180, 0, 0}, {EQK_PEAK, 3000, 1.2f, 5}}},
    {"muffled and boomy", "both problems at once: -5 dB below 300 Hz, +7 dB above 2.5 kHz, 150 Hz high-pass", -2.0f, 3,
     {{EQK_HP, 150, 0, 0}, {EQK_LSHELF, 300, 0, -5}, {EQK_HSHELF, 2500, 0, 7}}},
    {"clarity", "speech clarity: 200 Hz high-pass, +3 dB at 1.5 kHz, +4 dB above 3 kHz", -2.0f, 3,
     {{EQK_HP, 200, 0, 0}, {EQK_PEAK, 1500, 1.0f, 3}, {EQK_HSHELF, 3000, 0, 4}}},
};

const eq_profile_t eq_spk_profiles[EQ_PROFILES] = {
    {"flat", "no equalization", 0.0f, 0, {{0}}},
    {"telephone", "300 Hz to 3.4 kHz only: authentic, and hides the amplifier hiss", 0.0f, 2,
     {{EQK_HP, 300, 0, 0}, {EQK_LP, 3400, 0, 0}}},
    {"tame harsh", "harsh small receiver: -4 dB at 2.5 kHz, -3 dB above 4.5 kHz", 0.0f, 2,
     {{EQK_PEAK, 2500, 1.5f, -4}, {EQK_HSHELF, 4500, 0, -3}}},
    {"bass lift", "receiver not sealed to the ear: +6 dB below 350 Hz, 120 Hz high-pass", -6.0f, 2,
     {{EQK_HP, 120, 0, 0}, {EQK_LSHELF, 350, 0, 6}}},
    {"clarity", "clearer speech: 250 Hz high-pass, +3 dB at 2.8 kHz", -3.0f, 2,
     {{EQK_HP, 250, 0, 0}, {EQK_PEAK, 2800, 1.2f, 3}}},
    {"warm", "softer and rounder: +3 dB below 500 Hz, -5 dB above 3 kHz", -3.0f, 2,
     {{EQK_LSHELF, 500, 0, 3}, {EQK_HSHELF, 3000, 0, -5}}},
    {"intelligibility", "for hard of hearing: 300 Hz high-pass, +4 dB at 2 kHz, +2 dB above 4 kHz", -6.0f, 3,
     {{EQK_HP, 300, 0, 0}, {EQK_PEAK, 2000, 1.0f, 4}, {EQK_HSHELF, 4000, 0, 2}}},
    {"low hiss", "cut the highs where the hiss lives: 4 kHz low-pass, -6 dB above 3 kHz", 0.0f, 2,
     {{EQK_LP, 4000, 0, 0}, {EQK_HSHELF, 3000, 0, -6}}},
    {"loud", "more perceived loudness: 350 Hz high-pass, +3 dB at 1.2 kHz", -3.0f, 2,
     {{EQK_HP, 350, 0, 0}, {EQK_PEAK, 1200, 1.0f, 3}}},
    {"soft wideband", "keep the low end and tame the top: +3 dB below 300 Hz, -4 dB above 5 kHz, 100 Hz high-pass", -3.0f, 3,
     {{EQK_HP, 100, 0, 0}, {EQK_LSHELF, 300, 0, 3}, {EQK_HSHELF, 5000, 0, -4}}},
};

// ---- Biquad engine ----
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} bq_t;

typedef struct {
    volatile bool active;
    volatile int index;
    float pre;
    int n;
    bq_t bq[EQ_MAX_SECT];
} chain_t;

static chain_t chain_mic, chain_spk;
static int rate = 16000;

static void design(bq_t *q, const eq_sect_t *s, float fs)
{
    float w0 = 2.0f * 3.14159265f * s->f / fs;
    float c = cosf(w0), sn = sinf(w0);
    float qq = s->q > 0.0f ? s->q : 0.70710678f;
    float b0, b1, b2, a0, a1, a2;
    switch (s->kind) {
    case EQK_HP: {
        float alpha = sn / (2.0f * qq);
        b0 = (1.0f + c) / 2.0f;
        b1 = -(1.0f + c);
        b2 = b0;
        a0 = 1.0f + alpha;
        a1 = -2.0f * c;
        a2 = 1.0f - alpha;
        break;
    }
    case EQK_LP: {
        float alpha = sn / (2.0f * qq);
        b0 = (1.0f - c) / 2.0f;
        b1 = 1.0f - c;
        b2 = b0;
        a0 = 1.0f + alpha;
        a1 = -2.0f * c;
        a2 = 1.0f - alpha;
        break;
    }
    case EQK_PEAK: {
        float A = powf(10.0f, s->db / 40.0f);
        float alpha = sn / (2.0f * qq);
        b0 = 1.0f + alpha * A;
        b1 = -2.0f * c;
        b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = -2.0f * c;
        a2 = 1.0f - alpha / A;
        break;
    }
    case EQK_LSHELF: {
        float A = powf(10.0f, s->db / 40.0f);
        float alpha = sn / 2.0f * sqrtf(2.0f);                   // slope 1
        float sq = 2.0f * sqrtf(A) * alpha;
        b0 = A * ((A + 1.0f) - (A - 1.0f) * c + sq);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * c - sq);
        a0 = (A + 1.0f) + (A - 1.0f) * c + sq;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * c);
        a2 = (A + 1.0f) + (A - 1.0f) * c - sq;
        break;
    }
    default: {                                                    // EQK_HSHELF
        float A = powf(10.0f, s->db / 40.0f);
        float alpha = sn / 2.0f * sqrtf(2.0f);
        float sq = 2.0f * sqrtf(A) * alpha;
        b0 = A * ((A + 1.0f) + (A - 1.0f) * c + sq);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * c - sq);
        a0 = (A + 1.0f) - (A - 1.0f) * c + sq;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * c);
        a2 = (A + 1.0f) - (A - 1.0f) * c - sq;
        break;
    }
    }
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    q->x1 = q->x2 = q->y1 = q->y2 = 0.0f;
}

static void load(chain_t *c, const eq_profile_t *p, int index)
{
    c->active = false;                                            // the audio task skips it while it is rewritten
    c->index = index;
    c->n = p->n;
    c->pre = powf(10.0f, p->pre_db / 20.0f);
    for (int i = 0; i < p->n; i++) {
        design(&c->bq[i], &p->s[i], (float)rate);
    }
    c->active = index != 0 && p->n > 0;
}

void eq_set_rate(int fs)
{
    rate = fs;
    load(&chain_mic, &eq_mic_profiles[chain_mic.index], chain_mic.index);
    load(&chain_spk, &eq_spk_profiles[chain_spk.index], chain_spk.index);
}

void eq_select(bool mic, int index)
{
    if (index < 0 || index >= EQ_PROFILES) {
        return;
    }
    load(mic ? &chain_mic : &chain_spk, mic ? &eq_mic_profiles[index] : &eq_spk_profiles[index], index);
}

int eq_selected(bool mic)
{
    return mic ? chain_mic.index : chain_spk.index;
}

static inline float run_chain(chain_t *c, float x)
{
    x *= c->pre;
    for (int i = 0; i < c->n; i++) {
        bq_t *q = &c->bq[i];
        float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
        q->x2 = q->x1;
        q->x1 = x;
        q->y2 = q->y1;
        q->y1 = y;
        x = y;
    }
    return x;
}

void eq_process_stereo(bool mic, int16_t *st, int frames)
{
    chain_t *c = mic ? &chain_mic : &chain_spk;
    if (!c->active) {
        return;
    }
    for (int i = 0; i < frames; i++) {
        float y = run_chain(c, (float)st[2 * i]);
        int16_t s = y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : (int16_t)y;
        st[2 * i] = s;
        st[2 * i + 1] = s;
    }
}

float eq_response_db(const eq_profile_t *p, float hz)
{
    float w = 2.0f * 3.14159265f * hz / (float)rate;
    float db = p->pre_db;
    for (int i = 0; i < p->n; i++) {
        bq_t q;
        design(&q, &p->s[i], (float)rate);
        float nr = q.b0 + q.b1 * cosf(w) + q.b2 * cosf(2 * w);
        float ni = -(q.b1 * sinf(w) + q.b2 * sinf(2 * w));
        float dr = 1.0f + q.a1 * cosf(w) + q.a2 * cosf(2 * w);
        float di = -(q.a1 * sinf(w) + q.a2 * sinf(2 * w));
        db += 10.0f * log10f((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return db;
}

void eq_bench(void)
{
    const int n = 16000;
    const double mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
    int saved = rate;
    rate = 16000;
    static int16_t buf[128 * 2];
    printf("CPU %.0f MHz; one second of 16 kHz audio through each profile (cycles per sample, share of one core):\n", mhz);
    double worst = 0;
    for (int which = 0; which < 2; which++) {
        const eq_profile_t *tab = which ? eq_spk_profiles : eq_mic_profiles;
        for (int i = 1; i < EQ_PROFILES; i++) {
            chain_t c = {0};
            load(&c, &tab[i], i);
            uint32_t t0 = xthal_get_ccount();
            for (int done = 0; done < n; done += 128) {
                for (int k = 0; k < 128; k++) {
                    buf[2 * k] = (int16_t)(((done + k) * 37) & 0x3fff) - 8192;
                }
                for (int k = 0; k < 128; k++) {
                    float y = run_chain(&c, (float)buf[2 * k]);
                    buf[2 * k] = (int16_t)y;
                }
            }
            uint32_t cyc = xthal_get_ccount() - t0;
            double per = (double)cyc / n, pct = 100.0 * (double)cyc / (mhz * 1e6);
            if (pct > worst) {
                worst = pct;
            }
            printf("  %s %d %-20s %3d sections: %5.0f cycles/sample = %.2f%%\n", which ? "spk" : "mic", i, tab[i].name, tab[i].n,
                   per, pct);
        }
    }
    printf("The most expensive profile costs %.2f%% of one core per direction (this includes generating the test signal, so "
           "it is an upper bound).\n", worst);
    rate = saved;
    eq_set_rate(saved);
}
