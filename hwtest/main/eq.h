// Equalizer profiles for the microphone and the earpiece (doc/initial_design.md, "Equalizer profiles").
//
// A profile is a few second-order sections (RBJ audio-EQ-cookbook biquads) plus a pre-gain to keep headroom. Index 0 is
// always "flat" (bypassed). Ten profiles per direction, so a dial digit 0-9 can select one.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define EQ_MAX_SECT 5
#define EQ_PROFILES 10

typedef enum { EQK_HP, EQK_LP, EQK_PEAK, EQK_LSHELF, EQK_HSHELF } eq_kind_t;

typedef struct {
    eq_kind_t kind;
    float f;      // corner or centre frequency, Hz
    float q;      // Q for high-pass, low-pass and peaking sections (0 = 0.707); ignored for shelves (slope 1)
    float db;     // gain for peaking and shelf sections
} eq_sect_t;

typedef struct {
    const char *name;
    const char *what;       // what it is for
    float pre_db;           // gain applied before the sections (negative when the sections boost)
    int n;
    eq_sect_t s[EQ_MAX_SECT];
} eq_profile_t;

extern const eq_profile_t eq_mic_profiles[EQ_PROFILES];   // anticipates the effects of a plastic housing
extern const eq_profile_t eq_spk_profiles[EQ_PROFILES];   // a small earpiece receiver

void eq_set_rate(int fs);                       // the codec sample rate changed (8000 or 16000): recompute the filters
void eq_select(bool mic, int index);            // 0 = flat
int eq_selected(bool mic);
// Process the left slot of an interleaved stereo buffer and copy it to the right slot (the device is mono). Does nothing
// when the profile is flat.
void eq_process_stereo(bool mic, int16_t *stereo, int frames);
// Magnitude response of a profile in dB at a frequency, at the current sample rate.
float eq_response_db(const eq_profile_t *p, float hz);
// Time one second of 16 kHz audio through every profile and print the CPU cost.
void eq_bench(void);
