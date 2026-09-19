# Audio Clips To Record

Running list of every recorded prompt the firmware needs. **Update this file whenever a feature adds,
changes or removes a spoken prompt.** Companion to `doc/initial_design.md` (the modal dial
interface) and `doc/tones.md` (tones, which are generated, not recorded).

Last updated: 2026-09-18.

## Format and storage

**Decided:** clips are stored on the **microSD card** (not in flash), one file per clip ID, so there can be
many more prompts and at higher quality.

| Item | Value |
|---|---|
| File format | 16-bit signed PCM mono WAV, named by clip ID (for example `num_5.wav`) |
| Sample rate | **not yet decided**: 16 kHz, 22.05 kHz or 44.1 kHz (the codec supports up to 48 kHz); tested in stage 8 of `validate_board_plan.md`. Record at the highest rate you may want and downsample later. |
| Channels | mono |
| Loudness | normalize all clips to the same peak level; the earpiece volume mode (mode 3) applies on top |
| Silence | trim leading and trailing silence to about 50 ms |
| Card | FAT32, 32 GB or smaller |
| Fallback | **Proposed:** a very small built-in set (or tones) for when the card is missing |

Prompts only play through the earpiece while the handset is off-hook and not in a call, so they are
free of the 8 kHz call-audio limit.

Convert with, for example: `ffmpeg -i in.wav -ar 22050 -ac 1 -sample_fmt s16 out.wav`

The recordings play through an earpiece, so keep the voice close-miked and intelligible.

**Voice and style:** not yet decided. One option that suits the project is a calm period-style
"telephone operator" delivery. Whichever voice is chosen, record every clip in the same session
and voice so they sound consistent.

## Clip list

Status values: `todo` (not recorded), `recorded`, `n/a` (dropped). Nothing is recorded yet.

### Numbers (used when announcing a setting)

| ID | Text | Used by | Status |
|---|---|---|---|
| `num_0` | "zero" | volume level 0 (loudest), era digit, digits read back | todo |
| `num_1` | "one" | same | todo |
| `num_2` | "two" | same | todo |
| `num_3` | "three" | same | todo |
| `num_4` | "four" | same | todo |
| `num_5` | "five" | same | todo |
| `num_6` | "six" | same | todo |
| `num_7` | "seven" | same | todo |
| `num_8` | "eight" | same | todo |
| `num_9` | "nine" | same | todo |

The digit `0` is the *loudest* volume and `1` is the quietest (see the volume mapping in
`initial_design.md`). Say the digit that was dialed, so "zero" means the loudest setting.

### Volume and setting confirmations

After a setting is changed, the firmware announces which value was set: the label clip, then the
digit.

| ID | Text | Used by | Status |
|---|---|---|---|
| `lbl_earpiece_volume` | "Earpiece volume" | mode 3, before the digit | todo |
| `lbl_mic_volume` | "Microphone volume" | mode 4, before the digit | todo |
| `lbl_tone_era` | "Tone era" | mode 5, before the digit | todo |

Optional, if you want a menu prompt on entering a mode (not decided):

| ID | Text | Used by | Status |
|---|---|---|---|
| `prm_pick_volume` | "Dial a number from one to zero" | modes 3 and 4 | todo (optional) |
| `prm_pick_era` | "Dial the era number" | mode 5 | todo (optional) |

### Tone eras (names read after selecting an era)

The final list depends on which tone profiles ship (see `tones.md`, section 10).

| ID | Text | Era | Status |
|---|---|---|---|
| `era_1_name` | e.g. "American, before nineteen sixty-five" | Profile A | todo |
| `era_2_name` | e.g. "American, standard" | Profile B | todo |
| `era_3_name` | e.g. "British, General Post Office" | Profile C (partial; pending) | todo |

### Bluetooth pairing (mode 2)

| ID | Text | When | Status |
|---|---|---|---|
| `pair_enter_code` | "Bluetooth pairing. Enter the six-digit code shown on your phone." | pairing started, passkey requested | todo |
| `pair_success` | "Paired." | pairing succeeded | todo |
| `pair_failed` | "Pairing failed." | authentication failed | todo |
| `pair_timeout` | "Pairing timed out." | pairing window expired | todo |

### Forget pairing (mode 6)

| ID | Text | When | Status |
|---|---|---|---|
| `forget_confirm` | "Dial six again to unpair. Hang up to cancel." | after dialing 6 | todo |
| `forget_done` | "Phone unpaired." | after confirming | todo |
| `forget_cancel` | "Cancelled." | any other digit dialed at the prompt | todo |

The confirm wording ("Dial six again") is from the design; the "Phone unpaired" wording is a
suggestion ("says now unpaired or something like that"). The cancel clip is only needed if a
different digit cancels rather than just being ignored (not decided).

### Errors and status

| ID | Text | When | Status |
|---|---|---|---|
| `err_no_phone` | "No phone is connected." | off-hook while Bluetooth is disconnected | todo (a period tone may be used instead) |
| `err_bad_mode` | "That option is not available." | first digit is an unassigned mode (0, 7, 8 or 9) | todo (a tone may be used instead) |
| `err_not_allowed` | "That number cannot be dialed." | a dialed number is blocked (if a blocklist is added) | todo (optional) |

The 10-digit call rule, the incomplete-number behavior and the off-hook timeout are handled by the
era's tones or silence (see "Behavior by era" in `initial_design.md` and `tones.md` section 7), so
they need no clip.

## Not needed

- Dial tone, busy, ringback, reorder, off-hook tone, and pairing beeps are **generated**, not
  recorded (see `tones.md`).
- No caller-ID readout: there is no display and no caller-ID feature.

## Notes for recording day

- Record the ten number clips in the same session as the labels so they blend naturally: the
  firmware plays them back to back (label, then number).
- Say "zero", not "oh".
- Keep each clip short. Long prompts are annoying on a novelty phone you might hear dozens of times.
- Record a couple of takes of the ones you expect to change (pairing and forget prompts).
