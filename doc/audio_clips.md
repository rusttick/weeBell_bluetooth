# Audio Clips To Record

Running list of every recorded prompt the firmware needs. **Update this file whenever a feature adds,
changes or removes a spoken prompt.** Companion to `doc/initial_design.md` (the modal dial
interface) and `doc/tones.md` (tones, which are generated, not recorded).

Last updated: 2026-09-28 (research on reading digit strings naturally added under "Numbers").

## Format and storage

**Decided:** clips are stored on the **microSD card** (not in flash), so there can be many more prompts and at higher
quality. **Use a single large WAV file (a "bank") holding all the clips, plus an index of clip boundaries** (below);
per-clip files (`num_5.wav`) remain the fallback if the stage 8 measurement says the bank is too slow.

- **File format:** 16-bit signed PCM mono WAV. One bank file `prompts.wav` plus an index `prompts.idx` whose entries
  are named by clip ID (for example `num_5`).
- **Sample rate:** **16 kHz, one bank** (see "One bank, 16 kHz" below). Stage 6 (2026-09-20): no audible quality
  difference at 1 kHz between 16, 22.05 and 44.1 kHz on the earpiece, so nothing higher is stored. Keep the original
  recordings at their highest rate and downsample from those.
- **Channels:** mono.
- **Loudness:** **Proposed:** loudness-normalize every clip to **-23 LUFS** in Audacity (Loudness Normalization, mono
  treated as single channel), then hard-limit peaks at **-1 dBFS**; the earpiece volume mode (mode 3) applies on top.
  See the Audacity procedure below.
- **Silence:** trim leading and trailing silence to about 50 ms.
- **Card:** FAT32, 32 GB or smaller.
- **Fallback:** **Proposed:** a very small built-in set (or tones) for when the card is missing.

Every clip plays through the earpiece outside a call, free of the 8 kHz call-audio limit. Convert with, for example:
`ffmpeg -i in.mp3 -ar 16000 -ac 1 -sample_fmt s16 out.wav`, or on macOS without ffmpeg:
`afconvert -f WAVE -d LEI16@16000 -c 1 -r 127 in.mp3 out.wav` (then normalize and trim).

### Single large WAV (a "bank") with an index

Yes. One 16 kHz mono 16-bit WAV can hold every clip back to back, with a small **index** that says where each clip
starts and how long it is:

- `prompts.wav`: an ordinary WAV (plays in any editor) holding all the clips one after another, with a little silence
  between them.
- `prompts.idx`: text, one line per clip: `clip_id,start_sample,length_samples` (for example `num_5,48000,6400`).

**How it plays:** the firmware opens `prompts.wav` once and reads the index into memory at start-up (500 clips is
about 12 KB). To play a clip it seeks to `data offset + start_sample × 2` and streams `length_samples × 2` bytes to the
codec, then can seek straight to the next clip for spoken numbers with no gap for opening a file.

**Why it is attractive**
- One file open instead of one per clip, so no directory search through hundreds of names, and no file-system
  waste (each small file wastes on average half a cluster).
- Fits the Audacity workflow: normalize the whole take once (as advised above), then put a **label** on each clip and
  use Tracks > Edit Labels > Export (a text file of start, end and name in seconds). A small script turns that into
  `prompts.idx` (sample = seconds × 16000). No splitting into files at all. Analyze > Label Sounds can place the labels
  automatically from the pauses.
- Contiguous data streams well.

**Costs**
- Changing one clip means rebuilding the bank and the index with a script (keep the source take and the labels).
- One bank per sample rate. A damaged bank loses every clip, so keep a backup.
- Seeking into a large file walks the FAT cluster chain, which can be slow unless the firmware builds with
  **`CONFIG_FATFS_USE_FASTSEEK`** (present in ESP-IDF 4.4.4), which keeps a cluster map for the file.

**Still to confirm:** measure it in stage 8: compare (a) opening a file by name among hundreds of files with (b) seeking
to random clips inside a bank of the same total size, with and without fast seek. If a seek plus the first read is
under about 20 ms, use the bank.

### One bank, 16 kHz

**Decided (2026-09-29): one bank only, `prompts.wav` + `prompts.idx` at 16 kHz.** The 8 kHz bank existed for
notifications played during a call, and none is left. No clip plays during a call: clips that follow a call (for
example "lost your telephone") start after the call audio has stopped. The firmware re-clocks the codec to 16 kHz at
call end, before any clip, and never re-clocks while a clip or call audio is playing.

### Getting the clip boundary metadata

The index needs, for each clip, its **name (the clip ID)** and its **start and end** inside the bank. Take them from
Audacity labels:

1. Import or assemble the take in Audacity, with the project rate at 16000 Hz. Put a short pause (at least 100 ms)
   between clips.
2. **Place the labels.** Either **Analyze > Label Sounds** (label type "region between sounds"; set the threshold
   near -40 dB and the minimum silence duration to about 0.3 s; check the labels line up with the clips), or click
   the start of a clip, hold Shift to select it, and press **Ctrl+B** to add a label by hand. Start each label about 30 ms
   before the speech and end it about 30 ms after, so the clip keeps a little padding.
3. **Type the clip ID into each label** (for example `num_5`), matching the IDs in the list below. IDs must be unique.
4. **Export the labels:** File > Export > **Export Labels...** (or Tracks > Edit Labels, then export). The result is a
   text file, one label per line, tab separated: `start_seconds<TAB>end_seconds<TAB>label`. For example:

   ```
   0.512000	1.023000	num_5
   1.980000	2.610000	num_6
   ```

   Ignore any line that starts with a backslash (Audacity writes those for labels with a frequency range).
5. **Convert seconds to samples** at the bank's rate: `start_sample = round(start_seconds × 16000)` and
   `length_samples = round((end_seconds − start_seconds) × 16000)`. Write one line per clip to `prompts.idx` as
   `clip_id,start_sample,length_samples`. A small script should also check that the clips do not overlap, that every
   ID appears once and is in the list below, and that the last clip ends inside the bank.
6. Export the audio as the bank: File > Export Audio > WAV (Microsoft), signed 16-bit PCM, 16000 Hz, mono.
   **Do not trim or change the audio after exporting the labels**, or the boundaries move: export the labels and the
   audio from the same project state.

If the clips are generated one by one (for example from a text-to-speech service) instead of recorded in one take,
a script that joins them can write the index directly, since it knows each clip's length in samples.

### Audacity procedure (loudness and format)

1. **Import** the clip. Tracks > **Resample** to **16000 Hz**, and check the Project Rate (bottom left) reads 16000 Hz.
2. **Trim** the silence before and after the speech to about 50 ms, and add a few milliseconds of fade in and out.
3. Effect > **Loudness Normalization**: normalize *perceived loudness* to **-23 LUFS**. **Untick "Treat mono as
   dual-mono"** if your version has it (with it ticked a mono clip reads 3 dB louder, so use -20 LUFS instead; what
   matters is that every clip uses the same setting). For the sample clip this is about +7.7 dB, giving a peak near
   -2.5 dBFS.
4. Effect > **Limiter**: *hard limit*, limit to **-1.0 dB**, make-up gain 0%. This only acts on a clip whose peaks would
   pass -1 dBFS (clips with a high crest factor).
5. File > Export Audio > **WAV (Microsoft), Signed 16-bit PCM**, project rate 16000 Hz, mono. Leave the metadata empty.
6. **Check** the result: the peak (Effect > Amplify shows it; cancel afterwards) is at most -1 dBFS, and the loudness
   is -23 LUFS (Loudness Normalization again shows no change).

**Cautions**
- **Very short clips** (single digits, under about 0.4 s) are too short for a reliable LUFS measurement, which uses
  400 ms blocks. Record or generate a set of related clips in one take, normalize the take once, then split it into
  clips so their relative loudness is kept.
- **Batch:** Tools > Macros can apply these steps to many files (Apply to Files). Try it on two or three first.
- The absolute level is not critical, since the earpiece volume and the earpiece pad resistors (160 Ω per leg now) set the final loudness; **consistency
  between clips is the point**. Judge the final level by ear on the earpiece, with real prompts.

### Sample analysis: `camilla_montgomery.mp3` (2026-09-20)

- **Container:** MP3, mono, 44.1 kHz, constant 128 kbit/s (LAME-type encoder, "Lavf60.16.101"); 3.37 s.
- **File size:** 70,981 bytes: 53,916 of audio plus about 17 KB of metadata (an ID3 tag carrying a C2PA "content
  credentials" manifest). The metadata is dropped in a WAV.
- **Bandwidth:** nothing above 16 kHz (the encoder's low-pass). 95% of the energy is below 3 kHz, 99% below 7.4 kHz,
  99.9% below 11 kHz. 4 to 8 kHz holds 3.3% of the energy (the "s" and "f" sounds).
- **Level:** peak -10.2 dBFS, RMS -30.0 dBFS (crest factor 19.8 dB): quiet. About +9.2 dB of gain brings the peak to
  -1 dBFS (RMS -21 dBFS).
- **Silence:** 0.08 s before the speech and about 0.34 s after it (below -50 dBFS); noise floor -75.7 dBFS.

**Why 16 kHz, mono, 16-bit WAV**
- **Uncompressed WAV:** the SD card is not a constraint (see the table below), and WAV costs no CPU and adds no
  artifacts, while an MP3 decoder would compete with Bluetooth for the ESP32. The source is already lossy, so do not
  compress it a second time.
- **16 kHz keeps the speech and drops nothing you can hear:** 99% of the energy is below 7.4 kHz, so 16 kHz sampling
  (8 kHz bandwidth) keeps the consonants; 8 kHz sampling (4 kHz bandwidth) would lose the "s" and "f" detail; 22.05 and
  44.1 kHz only store empty spectrum for a small earpiece. It is also the **wide-band call rate**, so the same codec
  clock serves prompts and wide-band calls.
- **Size:** 32 kB/s, so this clip is about 108 kB (71 KB as the MP3, 58 KB at 8 kHz); 100 MB holds about 52 minutes.
- **Level:** normalize every clip to the same loudness, for example a peak of -1 dBFS (about RMS -21 dBFS for this
  clip). The codec is then near full scale and the signal-to-noise ratio is best (stage 6). Speech at RMS -21 dBFS is
  about 17 dB quieter than the -1 dBFS test tone used to set the earpiece level.
- **Trim** to about 50 ms before and after the speech and add a few milliseconds of fade in and out, so no click
  reaches the amplifier.

| Rate | Data rate | Minutes in 100 MB (mono, 16-bit) |
|---|---|---|
| 8 kHz | 16 kB/s | 104 |
| 16 kHz | 32 kB/s | 52 |
| 22.05 kHz | 44.1 kB/s | 38 |
| 44.1 kHz | 88.2 kB/s | 19 |

The recordings play through an earpiece, so keep the voice close-miked and intelligible.

**Voice and style:** not yet decided. One option that suits the project is a calm period-style
"telephone operator" delivery. Whichever voice is chosen, record every clip in the same session
and voice so they sound consistent.

## Clip list

Status values: `todo` (not recorded), `recorded`, `n/a` (dropped). Nothing is recorded yet. Tables list only the ID,
the text and the status; where each clip is used is in the notes under each table and in `phone_ui.md`.

### Numbers (used when announcing a setting)

| ID | Text | Status |
|---|---|---|
| `num_0` | "zero" | todo |
| `num_1` | "one" | todo |
| `num_2` | "two" | todo |
| `num_3` | "three" | todo |
| `num_4` | "four" | todo |
| `num_5` | "five" | todo |
| `num_6` | "six" | todo |
| `num_7` | "seven" | todo |
| `num_8` | "eight" | todo |
| `num_9` | "nine" | todo |

Used after a label to confirm a setting, for phone numbers and for any digits read back. `num_0` is used for
"equalizer off"; it is no longer a volume level. Volume uses only `1` (quietest) to `9` (loudest) since 2026-09-28 (see
the volume model in `initial_design.md`).

### Reading digit strings naturally (research, 2026-09-28)

A single recording of each digit played back to back sounds robotic, because a person speaking a phone number changes
the pitch of a digit by its position. The established method for building natural phone-number readout from a minimal
set of recordings is **US Patent 6,601,030 B2, "Method and system for recorded word concatenation"** (Ann K. Syrdal,
AT&T, filed 1998, later Nuance; expired): <https://patents.google.com/patent/US6601030B2/en>

**Three pitch patterns, chosen by position, not by digit.** The patent names the positions of a 10-digit number after
its example `(123) 456-7890` and marks each with a ToBI (Tones and Break Indices) intonation pattern:

- **Positions 1, 2, 4, 5, 7, 8, 9:** `H*`, a plain accented digit, level.
- **Positions 3 and 6** (end of the area code and of the exchange): `H* L-H%`, a continuation rise. The pitch dips, then
  rises at the group break ("more coming").
- **Position 10** (the last digit): `H* L-L%`, a final fall. The pitch drops ("done").

Method details:

- **Inventory:** 10 digits × 3 patterns = **30 clips**. The patent recommends **two or more takes** of each digit and
  pattern, picked at random without replacement during playback, so a string such as 555-5555 does not sound like the
  same clip repeated.
- **Script:** record real phone-number strings (the patent's examples are "672-1288" and "380-1489"), spoken
  "naturally but clearly and carefully", and cut the target digits out of them.
- **Coarticulation:** choose the digit before each target so that its last sound is made at the same place in the
  mouth as the target's first sound (for example /uw/ then /w/: "two" before "one"). The cut-out digit then joins its
  new neighbour without a mismatch.
- **Cutting:** keep 0 to 50 ms of silence before and after an `H*` or `H* L-L%` target. An `H* L-H%` target may keep
  some or all of the pause that follows it, so the group break travels with the clip.

**Earlier alternative:** US Patent 5,740,319, "Prosodic number string synthesis" (Frederick C. Wedemeier, Texas
Instruments, filed 1993): <https://patents.google.com/patent/US5740319A/en>. It needs **130 segments**: the ten digits at
the start of a string, at the end, and before a group pause, plus all 100 digit pairs 00 to 99, recorded at constant
pitch, cadence and volume except for the final and pre-pause digits. Each digit is split in two halves and a number is
built from pairs (`322-2333` = `3.1 + 2|2 + 2|3 + 3.p + 2|3 + 3|3 + 3.t`). It joins more smoothly but needs four
times the clips and exact cut points.

**Not needed for now (2026-09-29):** the dialed number is never read back, and confirmations are generated as whole
sentences (`voice/tts_script.txt`), so no digit strings are joined. Kept for reference: replace `num_0` … `num_9` with the 30-clip set, `num_<d>_h`, `num_<d>_lh` and
`num_<d>_ll`, ideally two takes each. A single digit after a label ("Earpiece volume … seven") uses the final-fall
`_ll` clip. With a text-to-speech service, generate whole numbers written with group punctuation (for example "six
seven two, one two eight eight.") and cut the target digits using the service's word timestamps, rather than
generating isolated digits.

### Labels (setting names)

| ID | Text | Status |
|---|---|---|
| `lbl_earpiece_volume` | "Earpiece volume" | todo |
| `lbl_mic_volume` | "Microphone volume" | todo |
| `lbl_tone_era` | "Tone era" | todo |
| `lbl_mic_eq` | "Microphone equalizer" | n/a |
| `lbl_earpiece_eq` | "Earpiece equalizer" | n/a |

Each label is spoken on entering its mode (3, 4, 5, 7, 8) and again before the digit when a setting is confirmed. For
the equalizers, digit zero means off. For the earpiece volume the new level is applied first and `vol_sample` follows
the digit, so the confirmation doubles as a sample of the new loudness (decided 2026-09-28).

### Mode instructions and samples

| ID | Text | Status |
|---|---|---|
| `prm_pick_volume` | "Dial one for the quietest, up to nine for the loudest." | todo (decided) |
| `vol_sample` | "This is how loud calls will sound." | todo (decided; wording open) |
| `prm_pick_era` | "Dial one, two or three." | todo (proposed) |
| `prm_pick_eq` | "Dial one to nine to choose a setting, or zero for none." | n/a |

Notes:

- The operator (mode 0) only names each mode; the mode explains what to dial when it is entered. Decided for modes 3
  and 4, proposed for 5, 7 and 8 (`phone_ui.md`).
- `prm_pick_volume`: modes 3 and 4, right after the label, on entering the mode.
- `vol_sample`: after the label and digit confirming a new earpiece level, played at that level as a sample.
- `prm_pick_era`: mode 5, after the label. `prm_pick_eq`: modes 7 and 8, after the label.

### Tone eras (names read after selecting an era)

The final list depends on which tone profiles ship (see `tones.md`, section 10).

| ID | Text (example) | Era | Status |
|---|---|---|---|
| `era_1_name` | "American, before nineteen sixty-five" | A | todo |
| `era_2_name` | "American, standard" | B | todo |
| `era_3_name` | "British, General Post Office" | C | todo |

Era C is partial (see `tones.md`), but it stays in the design. These era clips are superseded by the era prompt and
description lines in `voice/tts_script.txt`.

### Bluetooth pairing (mode 2)

| ID | Text | Status |
|---|---|---|
| `pair_enter_code` | see the note below | todo (stale) |
| `pair_success` | "Paired." | todo |
| `pair_failed` | "Pairing failed." | todo |
| `pair_timeout` | "Pairing timed out." | todo |

Notes:

- `pair_enter_code`: "Bluetooth pairing. Enter the six-digit code shown on your phone." It is from the old passkey
  plan, which fails on an iPhone. It is to be replaced by `pair_start`, which tells the user to choose the phone in
  their Bluetooth settings (`phone_ui.md`, "2: Bluetooth pairing").
- `pair_success` plays when pairing succeeds, `pair_failed` on an authentication failure, and `pair_timeout` when the
  pairing window expires.

### Forget pairing (mode 6)

| ID | Text | Status |
|---|---|---|
| `forget_confirm` | "Dial six again to unpair. Hang up to cancel." | todo |
| `forget_done` | "Phone unpaired." | todo |
| `forget_cancel` | "Cancelled." | todo |

`forget_confirm` plays after dialing 6 and `forget_done` after confirming. The confirm wording ("Dial six again") is
from the design; the "Phone unpaired" wording is a suggestion ("says now unpaired or something like that"). The cancel
clip is only needed if a different digit dialed at the prompt cancels rather than just being ignored (not decided).

### Operator (mode 0)

Dialing `0` reaches the "operator" (decided 2026-09-28). The ideal is what a real operator did when you dialed `0`; for
now it only states each top-level mode, not nested. **Changed (2026-09-29):** the whole list is one clip per persona,
`op_list` in `voice/tts_script.txt`, which supersedes the per-line clips below.

| ID | Text | Status |
|---|---|---|
| `op_intro` | "Operator." | todo (optional) |
| `op_mode_1` | "Dial one and then the ten-digit number to place a call." | todo |
| `op_mode_2` | "Dial two to pair a phone." | todo |
| `op_mode_3` | "Dial three to set the earpiece volume." | todo |
| `op_mode_4` | "Dial four to set the microphone volume." | todo |
| `op_mode_5` | "Dial five to choose the tone era." | todo |
| `op_mode_6` | "Dial six to forget the paired phone." | todo |
| `op_mode_7` | "Dial seven to choose the microphone equalizer." | n/a |
| `op_mode_8` | "Dial eight to choose the earpiece equalizer." | n/a |
| `op_hangup` | "Hang up at any time to cancel." | todo (optional) |

`op_intro` starts the list and `op_hangup` ends it. The equalizer clips (`lbl_*_eq`, `prm_pick_eq`, `op_mode_7`,
`op_mode_8`) are dropped: `7` and `8` are unassigned modes (2026-09-28); `9` is the voice assistant.

### Errors and status

| ID | Text | Status |
|---|---|---|
| `err_no_phone` | "No phone is connected." | todo |
| `err_bad_mode` | "That option is not available." | todo |
| `err_not_allowed` | "That number cannot be dialed." | todo (optional) |

Notes:

- `err_no_phone`: off-hook while Bluetooth is disconnected. A period tone may be used instead.
- `err_bad_mode`: the first digit is an unassigned mode (`7` or `8`). Replaced by a witty remark per digit and era
  (`err_mode_7` and `err_mode_8` in `voice/tts_script.txt`).
- `err_not_allowed`: a dialed number is blocked, only if a blocklist is added.

### Active notifications (can play during a call)

None are planned; no clip plays during a call.

| ID | Text | Status |
|---|---|---|
| `note_battery_low` | "Battery low." | n/a |

`note_battery_low` is obsolete (2026-09-28): the phone has no battery and runs from USB (`ringer_driver_2.md`). No
active notification is left, so the 8 kHz bank is dropped (2026-09-29).

The 10-digit call rule, the incomplete-number behavior and the off-hook timeout are handled by the
era's tones or silence (see "Behavior by era" in `initial_design.md` and `tones.md` section 7), so
they need no clip.

## Sound effects to record (not text-to-speech)

**To do:** record the operator hanging up (`sfx_hangup`). It plays after her last waiting remark, just before dial
tone returns (`phone_ui.md`, "Waiting in a mode"). Record a real handset being put down on its cradle, ideally a period
phone, close-miked, about one second including the switch click.

- Save the original recording as `voice/recordings/sfx_hangup.wav`, at its highest rate.
- It goes into the bank like any other clip, with the ID `sfx_hangup`: trimmed, faded, and at a level that sits
  naturally after the speech (not loudness-normalized like speech).

**To do:** find hold music for era 2 (`sfx_hold_music_era2`). It plays under the recording's check-ins during the
pairing window (`phone_ui.md`, "2: Bluetooth pairing"). Look for a public-domain or royalty-free instrumental in late
1960s to 1970s easy-listening style, at least 3 minutes long or cleanly loopable. Save the original as
`voice/recordings/sfx_hold_music_era2.wav` and note its source and licence next to it.

The pairing-window hold scenes (`hold_scene`, one per persona) are assembled by hand in Audacity from the `hold_*`
clips and the `@pause` timings in `voice/tts_script.txt`; the era 2 scene is mixed over the hold music.

| ID | Sound | Status |
|---|---|---|
| `sfx_hangup` | the operator hanging up | todo |
| `sfx_hold_music_era2` | era 2 hold music (find, do not record) | todo |
| `hold_scene` | one per persona, assembled from the script | todo |

## Not needed

- Dial tone, busy, ringback, reorder, off-hook tone, and pairing beeps are **generated**, not
  recorded (see `tones.md`).
- No caller-ID readout: there is no display and no caller-ID feature.

## Notes for recording day

- Record the ten number clips in the same session as the labels so they blend naturally: the
  firmware plays them back to back (label, then number).
- Say the digit 0 the way the persona's era would have: "oh" (or "operator", the label on the US dial's 0 hole) for
  the 1940s-50s US and UK operators, "zero" for the 1965+ Bell System recording (not verified). Details are in the notes
  at the top of `voice/tts_script.txt`.
- Keep each clip short. Long prompts are annoying on a novelty phone you might hear dozens of times.
- Record a couple of takes of the ones you expect to change (pairing and forget prompts).
