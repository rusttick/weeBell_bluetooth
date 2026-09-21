// Console command groups for the hardware validation firmware.
#pragma once
#include <stdbool.h>

void register_sys_commands(void);   // info, psram, i2cscan, hello
void register_gpio_commands(void);  // pins, mode, set, watch, boot
void register_codec_commands(void); // codec regs | rw | init
void register_audio_commands(void); // audio, tone, vol, volstep, mute, eq
void register_mic_commands(void);   // mic
void register_sd_commands(void);    // sd
void register_bt_commands(void);    // bt
void register_eq_commands(void);    // eqp
void register_rec_commands(void);   // rec

// SD card (cmds_sd.c): is a card mounted (and mounted read-write if `write`)? Prints the reason if not.
bool sd_ready(bool write);

// Capture the candidate pins' levels before anything configures them, then set them all to plain
// inputs (no pulls) and start the edge-watch task. Call first thing in app_main.
void gpio_tools_init(void);

// Put the candidate pins back to plain inputs with no pulls (used after tests that touch them).
void gpio_tools_restore_floating(void);

// Drive a test pin as an output / release it back to a plain input (used for the speaker-amp enable, IO21).
bool gpio_tools_drive(int pin, int level);
void gpio_tools_release(int pin);

// Codec helpers (cmds_codec.c), used by the audio commands.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
bool codec_ready(void);                            // sets up I2C on first use; prints and returns false on failure
esp_err_t codec_write(uint8_t reg, uint8_t val);
int codec_init(void);                              // reset and run the init sequence; 0 = ok

// Audio helpers (cmds_audio.c), used by the mic commands.
bool audio_is_on(void);
int audio_rate(void);
int audio_read_frames(int16_t *buf, int frames, int timeout_ms);   // the microphone (its slot copied to both) after the mic profile
int audio_read_frames_raw(int16_t *buf, int frames, int timeout_ms); // interleaved L,R exactly as the codec delivers them
int mic_source_slot(void);                                         // which I2S slot carries the microphone: 1 right, 0 left, -1 both
void audio_source_pause(void);                                     // stop the tone task so another source can write
void audio_source_resume(void);
int audio_write_frames(const int16_t *stereo, int frames);         // interleaved L,R; returns frames written, -1 on error
int audio_restart(int rate);                                       // restart the codec and I2S at this rate; 0 = ok

// Microphone input setup (cmds_mic.c); called by `audio on` after the codec is started.
void mic_apply(void);
