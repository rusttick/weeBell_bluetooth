# Audio Clips To Record

Running list of every recorded prompt the firmware needs. **Update this file whenever a feature adds,
changes or removes a spoken prompt.** Companion to `doc/initial_design.md` (the modal dial
interface) and `doc/tones.md` (tones, which are generated, not recorded).

Last updated: 2026-09-20 (both 8 kHz and 16 kHz banks decided; battery-low notification added).

## Format and storage

**Decided:** clips are stored on the **microSD card** (not in flash), so there can be many more prompts and at higher
quality. **Use a single large WAV file (a "bank") holding all the clips, plus an index of clip boundaries** (below);
per-clip files (`num_5.wav`) remain the fallback if the stage 8 measurement says the bank is too slow.

| Item | Value |
|---|---|
| File format | 16-bit signed PCM mono WAV. One bank file `prompts.wav` plus an index `prompts.idx` whose entries are named by clip ID (for example `num_5`). |
| Sample rate | **Decided: two banks, 8 kHz and 16 kHz** (see "Two banks" below). Stage 6 (2026-09-20): no audible quality difference at 1 kHz between 16, 22.05 and 44.1 kHz on the earpiece, so nothing higher is stored. Keep the original recordings at their highest rate and downsample from those. |
| Channels | mono |
| Loudness | **Proposed:** loudness-normalize every clip to **-23 LUFS** in Audacity (Loudness Normalization, mono treated as single channel), then hard-limit peaks at **-1 dBFS**; the earpiece volume mode (mode 3) applies on top. See the Audacity procedure below. |
| Silence | trim leading and trailing silence to about 50 ms |
| Card | FAT32, 32 GB or smaller |
| Fallback | **Proposed:** a very small built-in set (or tones) for when the card is missing |

Most prompts play through the earpiece while the handset is off-hook and not in a call, free of the 8 kHz call-audio
limit. **Some notifications can play during a call** (for example "battery low"), so both an 8 kHz and a 16 kHz bank are
needed ("Two banks" below).

**Both 8 kHz and 16 kHz** (see the sample analysis below). Convert with, for example:
`ffmpeg -i in.mp3 -ar 16000 -ac 1 -sample_fmt s16 out.wav`, or on macOS without ffmpeg:
`afconvert -f WAVE -d LEI16@16000 -c 1 -r 127 in.mp3 out.wav` (then normalize and trim); use `8000` for the other bank.

### Single large WAV (a "bank") with an index

Yes. One 16 kHz mono 16-bit WAV can hold every clip back to back, with a small **index** that says where each clip
starts and how long it is:

| File | Contents |
|---|---|
| `prompts.wav` | An ordinary WAV (plays in any editor) holding all the clips one after another, with a little silence between them. |
| `prompts.idx` | Text, one line per clip: `clip_id,start_sample,length_samples` (for example `num_5,48000,6400`). |

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

### Two banks: 8 kHz and 16 kHz

Store **both** `prompts_8k.wav` + `prompts_8k.idx` and `prompts_16k.wav` + `prompts_16k.idx` (both fit easily). Make the
8 kHz copy from the same normalized project by resampling offline (better than resampling in the firmware), then
limit again at -1 dBFS and export. Generate each index from the **same label file** with that bank's rate
(`sample = round(seconds × rate)`), so the two banks stay in step.

**Choosing the bank at run time.** Pick the bank that matches the **rate the codec's I2S clock is running at** when the
clip starts: `bank = (codec_rate == 16000) ? 16k : 8k`. The firmware already knows the rate:
- The current firmware runs the codec at a fixed **8 kHz** and up/down-samples wide-band call audio inside
  `audio_task`. The Bluetooth task learns the call codec from the HFP audio-state event
  (`ESP_HF_CLIENT_AUDIO_STATE_CONNECTED` = CVSD 8 kHz, `..._CONNECTED_MSBC` = mSBC 16 kHz; see `_bt_hf_client_audio_open`).
- **Idle** (off-hook, no call): use the 16 kHz bank, re-clocking I2S to 16 kHz first (or the 8 kHz bank if the codec is
  left at 8 kHz).
- **During a call** (only if a prompt is ever played then): the call's rate decides, 8 kHz for CVSD and 16 kHz for mSBC,
  so the clip matches the codec clock and needs no resampling.
- **Never re-clock the codec while a clip or a call's audio is playing.** Change the rate only at call start, call end
  or when idle. Stage 9 decides whether the codec should run at 16 kHz during wide-band calls (better speech than
  down-sampling to 8 kHz); if so the rule above already picks the right bank.

**Both banks are needed** because notifications such as "battery low" can play during a call, when the codec may be
running at 8 kHz (narrowband) or 16 kHz (wide-band).

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
- The absolute level is not critical, since the earpiece volume and the 820 Ω pad set the final loudness; **consistency
  between clips is the point**. Judge the final level by ear on the earpiece, with real prompts.

### Sample analysis: `camilla_montgomery.mp3` (2026-09-20)

| Property | Value |
|---|---|
| Container | MP3, mono, 44.1 kHz, constant 128 kbit/s (LAME-type encoder, "Lavf60.16.101"); 3.37 s |
| File size | 70,981 bytes: 53,916 of audio plus about 17 KB of metadata (an ID3 tag carrying a C2PA "content credentials" manifest). The metadata is dropped in a WAV. |
| Bandwidth | Nothing above 16 kHz (the encoder's low-pass). 95% of the energy is below 3 kHz, 99% below 7.4 kHz, 99.9% below 11 kHz. 4 to 8 kHz holds 3.3% of the energy (the "s" and "f" sounds). |
| Level | Peak -10.2 dBFS, RMS -30.0 dBFS (crest factor 19.8 dB): quiet. About +9.2 dB of gain brings the peak to -1 dBFS (RMS -21 dBFS). |
| Silence | 0.08 s before the speech and about 0.34 s after it (below -50 dBFS); noise floor -75.7 dBFS. |

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

### Active notifications (can play during a call)

Played with the bank that matches the codec's current rate (see "Two banks"). More may be added; list them here.

| ID | Text | When | Status |
|---|---|---|---|
| `note_battery_low` | "Battery low." | the battery voltage falls below the warning level, on or off a call | todo |

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
