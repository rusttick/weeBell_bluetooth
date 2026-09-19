# Rotary Phone → Bluetooth Handsfree: Initial Design

Follow-on from `doc/initial_research.md`. That doc recommended "ESP32 + weeBell-style
SLIC/codec board, Bluetooth HFP to your cellphone" as the pragmatic path. This doc
captures where the design actually landed after working through the hardware and
firmware in detail — which turns out to be simpler than a full weeBell/SLIC build,
because we're only salvaging two mechanical parts from the donor phone rather than
the whole electrical network.

Related documents:
- `doc/tones.md` — verified tones, dial protocol and service behavior for the era profiles.
- `doc/audio_clips.md` — running list of the spoken prompts that need recording.
- `doc/audio_kit_v2.2.md` — the development board: parts, pins, schematic findings.
- `doc/validate_board_plan.md` — step-by-step plan to prove the board works before building the firmware.

**Status of this document:** decisions are marked **Decided** (you stated them) or **Proposed**
(my recommendation, not yet confirmed). **No firmware has been changed yet**; everything under
"Firmware changes" is a plan. Last updated 2026-09-18.

## Purpose

This is a **novelty item**, not a functioning utility bluetooth headset. That drives several
decisions below (for example, it deliberately cannot dial emergency numbers).

## The donor phone

A "Kellogg" branded phone (Kellogg Switchboard & Supply Co., no model number found).
Identified network components: an induction/repeat coil marked "113A", a capacitor
marked "225" (almost certainly the ringer's DC-blocking capacitor), and a biased
mechanical ringer with two coils each marked "2000" (presumed 2000Ω each, ~4kΩ
combined in series). Exact ring voltage/frequency/cadence this phone was originally
designed for is unconfirmed — no date code or model number was found — so those
values are treated as bench-tunable parameters, not fixed specs (see Ring Driver
section).

**Only two mechanical parts are being reused: the rotary dial and the bell ringer
(coils + gongs + clapper).** The carbon microphone, receiver, and the entire network
(induction coil + capacitor) are being replaced with modern parts. This is a
significant simplification from the original weeBell approach:

## Why no SLIC

weeBell's AG1171 SLIC exists to emulate a real central office: supplying loop current
to a live 2-wire circuit, generating ring voltage, and providing the hybrid/anti-
sidetone function a real phone's network coil used to do. Since we're not preserving
the carbon mic or the network, there's no shared 2-wire loop to emulate — every
salvaged/added component gets its own dedicated, point-to-point connection to the
ESP32/codec instead:

- Hook switch → GPIO (digital in)
- Rotary dial pulse contacts (+ dial-in-progress shunt contact) → GPIO (interrupt/pulse count)
- Bell coils → dedicated ring-driver circuit (own supply, own GPIO control)
- Microphone → codec line-in
- Speaker/earpiece → codec line-out

No hybrid, no loop current, no line-voltage feed anywhere in the design.

## Hardware platform

**ESP32-A1S "Audio Kit" style board** (ESP32 module + onboard audio codec + battery
management on one PCB), replacing the original ELEGOO bare ESP32-WROOM-32 boards as
the final build target (those remain as spares/dev boards for other experiments).

- Bluetooth 4.2 BR/EDR + BLE, WiFi — Classic BT with SCO/HFP support confirmed present.
- Audio: LINEN stereo line input, HPOUT headphone output, LOUT/ROUT 3W amplified
  output. **The MAX9814 mic module's output goes to LINEN (line-in), not the raw
  mic-in path** — the MAX9814 is already amplified/AGC'd, so feeding it into a
  mic-level input designed for a raw capsule would double-amplify and likely clip.
- **Open item: confirm actual codec chip (ES8388 vs AC101) before committing.**
  The ESP32-A1S product line shipped with both across hardware revisions; this
  firmware's existing `es8388.c` driver only works with the ES8388. Verify by chip
  markings or I2C bus scan once the board is in hand — do not assume from listing
  text alone.
- **Board reference:** the full research on this board is in `doc/audio_kit_v2.2.md`. Your board is
  marked "V2.2 A618" with "k547" on the antenna; neither appears in any source, and "V2.2" boards ship
  with different codecs and different pin maps. Confirm the codec and pin map on the board first
  (that doc, section 9).
- **Input and output paths (important; confirmed by the official pin table and schematic):**
  on ES8388 V2.2 boards the line-in jack and the onboard microphones share the codec's LIN2/RIN2
  input, and LIN1/RIN1 is unused. So the MAX9814 goes into the line-in jack, the firmware must select
  `LINE2` for input (today it selects `LINE1`), and the onboard mics must be isolated (remove C18 and
  C20) so they don't mix into the handset mic. The headphone jack is the codec's **LOUT2/ROUT2**, so
  output must select `LINE2` too (the default `LINE1` is the speaker-amplifier path).
- **What the chips on the board are.** The Silicon Labs **CP2102** is the USB-to-UART bridge for
  programming and the serial console (UART0, GPIO1/3); it is **not** an amplifier. The audio codec is
  **inside the shielded ESP32-A1S module**, which is why the audio traces go straight into it. It can't
  be read off the board; identify it from an I2C scan (ES8388 at 0x10, AC101 at 0x1A). Two small
  class-D speaker amplifiers sit on the board and are **off by default** (a pull-down holds their enable
  line low); we use the codec's headphone output for the earpiece, so **leave GPIO21 alone**.
- **Pins change.** The firmware's current pin assignments are for the gCore board
  (I2C 21/22, I2S 25/19/26/34, ring 32/33, hook 35). The newest ES8388 A1S module puts the codec on
  I2C SDA 18 / SCL 23 and I2S BCLK 5 / WS 25 / DOUT 26 / DIN 35 / MCLK 0; older modules use I2C 33/32 and
  BCLK 27. Which one is on your board is unconfirmed (`doc/audio_kit_v2.2.md`, sections 4 and 9). Keep
  all pins in one header.
- **Only a few GPIOs are usable.** The header exposes only IO21, 22, 19, 23, 18, 5 (plus IO12, 13, 14, 15
  on the JTAG header). IO21 controls the speaker amps; IO23/18/5 are the codec's I2C and BCLK on the
  newest module; IO19 and IO23/18/5 are also wired into the button resistor ladder. We need **three
  inputs** (hook, dial pulse, dial-in-progress) and **two outputs** (DRV8825 STEP and nENABLE).
  **Proposed** (works on either module version): inputs on **IO13, IO15, IO14** via the JTAG header
  (DIP switches SW4 and SW5 ON, SW1–SW3 OFF); outputs **IO22 (STEP)** and **IO19 (nENABLE)**, removing
  resistor R67 to detach IO19 from the button ladder; external pull-ups on the inputs; the 10–100 Ω
  series resistor the spec recommends on each line; IO21 unused. Reasoning in `doc/audio_kit_v2.2.md`,
  section 6.
- Battery: 3.7V LiPo on the board's 2-pin battery connector (type and polarity not stated; check
  before connecting), with an onboard linear charger. **Correction to the first draft:** the
  schematic shows **no boost converter**. On battery the "5V" rail is simply the battery voltage
  (about 3.0–4.2 V), and the 3.3 V rail comes from a step-down regulator, so the board may brown out
  near 3.5 V battery voltage. This is a separate power domain from the ring-voltage boost chain (below),
  and the board's rail is not expected to supply the ring driver. There is no battery fuel gauge and the
  charger status goes only to LEDs, so firmware cannot read battery state unless an ADC divider is
  added later.
- Programming/debugging: standard UART bootloader over USB-serial (`idf.py flash`),
  no JTAG/debug probe needed. Note: a Pico Probe / probe-rs workflow does **not**
  apply here — the ESP32 is Xtensa, not RISC-V, and probe-rs's Espressif support only
  covers Espressif's RISC-V parts (C3/C2/C6/H2). Flashing is UART-only regardless.

## Microphone

**MAX9814 electret mic + AGC breakout**, 5 pins (VCC, GND, OUT, GAIN, AR).

- Only **3 wires travel down the handset cord**: VCC, GND, OUT. `GAIN` and `AR` are
  configuration pins, strapped locally at the board (or left floating for 60dB gain /
  default attack-release ratio) — not signals that need to reach the base.
- Module mounted **inside the handset**, next to the mouthpiece, not on the base PCB
  — so only the already-amplified, low-impedance, AGC'd signal travels the long
  (6-8ft) coiled cord, which is far more noise-resistant than running a raw
  high-impedance capsule signal that distance.
- Cord: a repurposed coiled 3-conductor (TRS) headphone extension cable — tip/ring
  carry mic-out and speaker signal, sleeve is shared ground.
- No carbon-mic-style DC bias/current-loop circuitry needed — this is a standard
  self-contained electret amp module, natively compatible with simple 3.3-5V supply.

## Ring driver

Bell coils: two ~2000Ω windings in series (~4kΩ combined), driven with an AC-like
square wave. Power draw is tiny (~2W at 90V, less at lower voltage) — current is
never the constraint here, voltage headroom is.

**Driver: DRV8825 stepper motor driver module**, run in "dumb" mode:

- Rated 8.2–45V, well above the ~36V a comparable reference build (a documented
  Arduino-based vintage-ringer project) reported as "ample for maximum volume" on a
  similar bell. 90V central-office spec is a conservative full-strength number, not
  a confirmed requirement for this phone.
- DRV8825 is a plain chopper-type driver (no StallGuard/CoolStep/sensorless smart
  features) — current-limit trimpot set to maximum so the chopper never engages
  against our ~4kΩ (way-below-trip-point) load, which makes it behave as a simple
  voltage-mode H-bridge rather than a current-regulated motor driver.
- One phase's H-bridge output drives the bell coils; STEP clock rate sets the output
  frequency arbitrarily and precisely (software-controlled, not fixed by any onboard
  oscillator) — full-step mode gives a direct, computable ratio between STEP rate and
  output frequency. (Full-step should give about four STEP pulses per output cycle;
  **verify on the bench**.)
- ENABLE/DIR/STEP are plain 3.3V logic, direct from ESP32 GPIO/timer/PWM.
- **Proposed:** the DRV8825 ENABLE pin is enabled by default through an internal
  pulldown, so add an **external pull-up on nENABLE** to keep the bell off while the
  ESP32's pins float during reset and boot.
- **Proposed:** generate STEP with a hardware timer (LEDC or MCPWM), not by toggling a
  pin from the firmware's 10 ms loop, which would jitter. Gate the bell with ENABLE and
  a cadence timer.
- **No series capacitor.** Considered as fault-tolerance insurance against a firmware
  hang leaving sustained DC across the coil, but decided against: at the actual
  operating voltage (running at roughly half the DRV8825's 45V ceiling, current
  limit wide open) the worst-case stuck-DC fault current into the ~4kΩ coils is only
  ~11mA even at the full 45V ceiling (`P ≈ 0.5W`) — well within what a ring-service
  coil tolerates indefinitely. Without the cap, a firmware fault just means the bell
  goes silent (clapper held to one side) until the fault clears, not a
  hardware-damaging condition. Not worth the part given how benign the failure mode
  already is. (A non-polarized/film capacitor, ~10-22µF at this coil's ~4kΩ
  impedance, remains the fix if this assessment changes — e.g. if a higher-voltage
  driver is adopted later.)
- Considered and rejected: two DRV8825 boards with tied inputs and series-stacked
  outputs to double voltage headroom (cascaded H-bridge / multilevel-inverter
  topology). Rejected because it requires fully galvanically isolated power *and*
  control interfaces per stage to avoid shorting through a shared ground — more
  isolation engineering than just using a single higher-voltage-rated driver.
- Fallback path if 45V proves insufficient on the actual bell: TB67S249FTG (47V,
  still "dumb"), then TMC5160 (up to 60V, a "smart" chip but usable in plain
  STEP/DIR standalone mode without engaging its sensorless features).
- Explicitly ruled out: BTS7960/IBT-2 (27V ceiling, monolithic — no MOSFET-swap
  escape hatch), reversed mains/doorbell transformers driven at 20Hz (real saturation
  risk at 1/3 of their rated frequency — a documented hobbyist build of this exact
  approach failed, producing pulsed/DC-like output instead of clean AC), EL-wire
  inverters (right voltage range, wrong/fixed frequency — 60Hz-2kHz internal
  oscillator, far too fast for a mechanical clapper to track), and industrial
  enclosed stepper drivers like DM556/DM860 (right voltage range, physically too
  large — ~4.6"×3.0"×1.3").

**Ring-driver power**: a two-stage boost from the LiPo, separate from the ESP32-A1S
board's own 5V system rail. Both stages use the same **XL6019-family adjustable
boost module** (3-35V in, 5-40V out, ~5A):
1. ~3V (battery) → ~12V
2. ~12V → adjustable, trimmed to whatever voltage bench testing shows is actually
   needed, up to the DRV8825's 45V ceiling

Using the same compact/low-power module family for both stages keeps the BOM simple.
Note this specifically does **not** cover a future upgrade to a higher-voltage driver
(TB67S249FTG @47V or TMC5160 @60V) — the compact/low-power modules available in that
45-90V range have a floor around 80-90V (nixie-tube-supply family), which would
overvolt the DRV8825 rather than trim down to it. If a higher-voltage driver is
adopted later, stage 2 needs to be swapped for one of those instead, not just
re-trimmed.

**Bench-test plan** (not yet executed): sweep drive frequency roughly 15-35Hz at
reduced voltage to find the bell's actual mechanical resonance (its real "hammer
frequency," rather than assuming the 20Hz US-standard default); then bring voltage up
to find the actual loudness-vs-voltage curve on this specific bell, rather than
assuming either the 90V central-office figure or the 36V reference-build figure
applies here.

**Ring cadence**: North American standard 2 seconds on / 4 seconds off (33% duty
cycle) is the default plan — configurable, easy to change, no reason yet to deviate.
The current firmware rings once per Bluetooth `RING_IND` from the cellphone, so the
cadence would depend on the phone's timing. **Proposed:** time the 2 s / 4 s cadence in
firmware from the first ring until the call is answered or ends (the app-task "last ring"
timeout is 7 s, which fits a 6 s cycle). The ring cadence is not part of the tone-era
profiles: profile C (UK) uses a different double-ring cadence (`tones.md`).

## Control interface — no touchscreen

The original gCore/LVGL touchscreen UI is being removed entirely. Everything the
screen used to do gets replaced by the phone itself.

**Decided:**
- `components/gui`, `components/gui_assets`, `components/lvgl*`, `gui_task.c/h`, and
  the display init in `main.c` get deleted.
- **RTC/time** is dropped entirely. The only place time was used was the
  `CLIP_DATETIME` field in outgoing Caller ID messages (`pots_task.c`), and Caller ID is
  being removed.
- **Bluetooth pairing, forget-pairing, volumes, and tone era** move to the modal dial
  interface below.

**Changed from the earlier draft:** locale and gain were going to be compile-time constants.
They are now user-selectable at run time by dial modes (below), so a small amount of
persistent storage is needed (see "Persistent settings").

### Modal dial interface

After the handset goes off-hook, the **first digit dialed selects a mode**. **Decided** (modes 1–6):

| First digit | Mode | Notes |
|---|---|---|
| `1` | Place a call | see "Dialing rules" |
| `2` | Bluetooth pairing | see "Bluetooth pairing via the dial itself" |
| `3` | Earpiece (speaker) volume | one digit sets it, then the number is spoken |
| `4` | Microphone volume | one digit sets it, then the number is spoken |
| `5` | Tone era | one digit selects an era profile (`tones.md`) |
| `6` | Forget pairing | dial `6` again to confirm; the phone then says it is unpaired |
| `0`, `7`, `8`, `9` | Unassigned | **Proposed:** an error indication (tone or clip) if dialed at the mode step |

Rules:
- **Once a mode is selected, digits 0-9 are all ordinary numeric input for that
  mode** — `0` has no special/global meaning inside a mode.
- **Going on-hook (hangup) unconditionally cancels whatever mode is active** and
  returns to idle. This still needs to be audited against the existing `app_task` state
  machine once implemented — the on-hook handler currently resets call-related
  states; it needs to also cleanly unwind any in-progress special-mode state (for
  example, mid-passkey-entry or a pending forget-pairing confirmation).
- The old "dial `0` alone starts voice assistant" behavior (`_appCanInitiateAssistantCall`) is
  not in the mode list. **Open:** drop it, or give it one of the unassigned digits.

### Dialing rules (mode 1) — Decided

- The `1` that selects the mode is consumed as a mode, then **exactly 10 more digits** are
  collected. The firmware then sends **`1` + those 10 digits** to the cellphone, so the number
  always carries the North American country code. This limits calls to North American Numbering
  Plan numbers (US, Canada and Caribbean nations, including toll-free and premium-rate numbers).
  A blocklist of area codes (for example 900 and some Caribbean codes) is optional.
- The call is placed **as soon as the 10th digit is dialed.** There is no fixed inter-digit
  timeout like the old 4 s wait; only a short (sub-1-second) pause is used to decide a single
  digit is finished.
- **911, 811 and 7-digit numbers are deliberately not dialable**, because nothing is sent until
  10 digits are entered. It is a novelty item, not a utility phone.
- **An incomplete number (fewer than 10 digits) is handled per era — Decided** (see "Behavior by
  era"): in the two step-by-step-style eras (US pre-1965, UK) you hear silence until you hang up
  or dial more, and in the US Precise era you get a reorder tone after about 16 seconds.
- Digits dialed while a call is already active are still sent as DTMF to the cellphone
  (existing behavior, unchanged).

### Volume model — Decided (mode 3 and 4)

- One digit sets the level. **Digit `1` is the quietest and `0` is the loudest** (`0` comes after
  `9` on the dial), **neither is zero or full-scale**, and the steps are **even in dB**.
- 10 levels, 11 equal intervals: digit `d` maps to step `k = d` (with `0` → `k = 10`) and
  `gain_dB = min_dB + (k / 11) × (max_dB − min_dB)`.
- **Starting range is wide, to be narrowed during testing.** Initial values use the existing
  `gain.h` limits:

| | Range | Step | Level `1` | Level `0` |
|---|---|---|---|---|
| Earpiece | −43.5 to +4.5 dB | 4.36 dB | ≈ −39.1 dB | ≈ +0.1 dB |
| Microphone | −39 to +9 dB | 4.36 dB | ≈ −34.6 dB | ≈ +4.6 dB |

- Expect the bottom steps to be too quiet to be useful (raise the floor after listening) and the
  microphone top step to risk clipping on top of the MAX9814's own gain/AGC (lower the ceiling if
  so). Only the two range constants change.
- After the level is set, the phone **speaks the number that was set** (`audio_clips.md`: a label
  clip such as "Earpiece volume", then the digit clip). The digit is the one dialed, so "zero"
  means loudest.
- These values persist across power cycles.

### Tone era — Decided (mode 5)

- One digit selects an era profile, and the phone says the number (and later the name) of the
  selected era. **Only verified tones are used** (`tones.md`). **Decided** assignments: **`1` = US
  pre-1965, `2` = US Precise Tone Plan (1965 onward), `3` = UK GPO Strowger** (only partly verified;
  its ringback tone is a flagged placeholder). No other eras for now.
- The era sets more than tones: it also sets the off-hook and partial-dial behavior and the busy
  tone used for a failed call (see "Behavior by era").
- The choice persists across power cycles.

### Bluetooth pairing via the dial itself

Standard headset pairing uses one of a few Bluetooth Secure Simple Pairing (SSP)
association models depending on each device's declared I/O capability:

- `ESP_BT_IO_CAP_IO` (Display+YesNo) → Numeric Comparison — what the current GUI
  build uses (shows a 6-digit code to eyeball-compare against the phone).
- `ESP_BT_IO_CAP_IN` (KeyboardOnly, no display) → **Passkey Entry** — the model this
  design uses instead: the iPhone displays a 6-digit passkey, and the "keyboard" that
  enters it back is the rotary dial.

Per the ESP-IDF header (`esp_gap_bt_api.h`), the SSP passkey is always exactly a
6-digit decimal number (`000000`-`999999`) — i.e. only digits 0-9, exactly what a
rotary dial already produces, no new decoding logic needed.

Implementation sketch in `bt_task.c`:
1. Change `iocap` (currently `ESP_BT_IO_CAP_IO` at `bt_task.c:794`) to
   `ESP_BT_IO_CAP_IN`.
2. On `ESP_BT_GAP_KEY_REQ_EVT`, enter a "collecting passkey" state and route the next 6
   dialed digits there instead of into the normal number buffer. **Note:** that event's case
   currently sits inside the `#ifdef BT_GAP_EVENT_DEBUG` block (it runs from about line 379 to
   425), so it is not compiled in a normal build. It has to be moved out of that block.
3. Once 6 digits are collected, call `esp_bt_gap_ssp_passkey_reply(bd_addr, true, passkey)`.
4. Use spoken clips for pairing feedback (start, success, failure, timeout; see
   `audio_clips.md`) in place of on-screen status, since there's no display.

**Forget pairing (mode 6) — Decided:** dialing `6` plays a prompt asking you to dial `6` again to
confirm; dialing `6` again removes the bond and says the phone is now unpaired. Hanging up cancels.
**Proposed:** dialing any other digit at the prompt also cancels.

### Hidden jobs the GUI did that must move elsewhere

Found while reading `gui_task.c` and `components/gui`; each needs a new home:

- Saving the pairing info when pairing succeeds, and clearing it when pairing is forgotten.
- The 60 s pairing timeout (`GUI_MAX_PAIR_MSEC`).
- Sending `BT_NOTIFY_ENABLE_PAIR`, `DISABLE_PAIR`, `FORGET_PAIR`, `CONFIRM_PIN`, `DENY_PIN` to `bt_task`.
- Mic mute (to `audio_task`) and ring mute / do-not-disturb (to `pots_task`).
- Country/locale selection (`POTS_NOTIFY_NEW_COUNTRY`), now replaced by the era mode.
- Codec gain updates from the settings screen (now the volume modes).

## Persistent settings — Proposed

`ps.c` stores everything in the gCore EFM8's RAM over I2C, which this board does not have. It
should be replaced with a small ESP-IDF NVS store (NVS is already initialised for Bluetooth bonds)
holding just:

- earpiece volume digit
- microphone volume digit
- tone era

Pairing state is best taken from the Bluetooth bond list itself, with a single-bond rule, instead
of a separate stored copy. That removes the case where the stored copy and the bond list disagree.
Brightness and country code are no longer needed.

## Tones and service behavior

Tones are **generated** in firmware, with the sources and numbers in `doc/tones.md`. Only verified
tones are included in the era profiles. Behaviors to implement, drawn from that document:

- The dial's off-normal contact mutes the earpiece while dialing (no pulse clicks in the audio),
  which is authentic.
- Dialed pulses are ignored while a busy tone plays.
- Ringback tone is generated locally when the cellphone reports the call is alerting (HFP
  `callsetup` = 3). **Open:** whether to mute the cellphone's own in-band ringback in the call
  audio so the period tone plays instead.
- Ringing stops immediately when the handset is lifted.

### Behavior by era — Decided

The era selected in mode 5 sets these behaviors as well as the tones. Each is matched to the era
as closely as the sources in `tones.md` (section 7) allow; the numbers marked "start" are values to
tune on the bench.

| Situation | US pre-1965 | US Precise (1965+) | UK GPO (partial) |
|---|---|---|---|
| Off-hook, nothing dialed | dial tone until you hang up or dial; no timeout | dial tone for about 50 s (start), then the off-hook howler | same as US pre-1965 (inferred) |
| Fewer than 10 digits dialed, then you stop | silence until you hang up or dial more | reorder tone after about 16 s (start) | same as US pre-1965 (inferred) |
| Call ends without being answered | busy tone, 60 per minute | busy tone, 0.5 s on / 0.5 s off | busy tone, 400 Hz, 0.75 s on / 0.75 s off |
| Dial pulses while busy plays | ignored | ignored | ignored |

- The no-timeout eras will feel like a dead phone if you stop mid-number; that is the documented
  behavior, and hanging up resets. **Proposed:** one compile-time constant per era (0 = no timeout)
  so it can be changed without code changes.
- The firmware **cannot distinguish a busy line from a failed call**: the cellphone's Bluetooth
  handsfree profile reports that the call ended, not why. Every unanswered end gets the era's busy
  tone.
- Ringback is generated locally per era, driven by the cellphone reporting that the call is alerting
  (see above).

## Audio clips

**Decided:** voice prompts are stored on the **microSD card**, so there can be many more of them and at
higher quality than would fit in flash. The full list to record, with status, is `doc/audio_clips.md`.

- **Format:** 16-bit mono WAV files, one per clip ID. The sample rate is **not yet decided**; the board's
  codec supports up to 48 kHz and the firmware's call audio path runs at 8 kHz (16 kHz for wide-band), so
  prompts may play at a higher rate by re-clocking I2S while the phone is idle. Stage 8 of
  `validate_board_plan.md` tests 16, 22.05 and 44.1 kHz.
- **SD wiring cost:** 1-bit SD mode uses **IO14 (CLK), IO15 (CMD) and IO2 (DATA0)**; 4-bit mode also uses IO4,
  IO12 and IO13. This **conflicts with the earlier GPIO proposal** (which put the hook and dial inputs on
  IO13, IO14 and IO15). Only about four clean GPIOs remain with the SD card (IO13, IO22, IO19, and IO21
  after removing R46), so a fifth GPIO, or dropping the dial-in-progress input, has to be decided (stage 4
  of the validation plan).
- **Risks to test:** some boards will not boot with a card inserted; the card slot lines are strapping-pin
  sensitive; a missing or failed card should not leave the phone silent (**Proposed:** keep a very small set
  of built-in fallback prompts, or fall back to tones). **Never format the card automatically** (the
  existing sample-recording code does; do not reuse it).
- **Proposed** playback implementation: a one-shot "clip" state in `pots_task` (the existing sample-based
  tone path loops forever), streaming from the file in chunks, using the same pattern DTMF and Caller ID
  audio already use to signal completion. Hanging up or dialing a digit interrupts a clip.

## Firmware changes (plan — nothing implemented yet)

Findings from reading the current firmware (`gcore_pots_bt/`). Why it fails on a bare board today:
`app_main` runs `ps_init()`, which talks to the missing gCore chip; `gcore_task` runs `power_init()`
against it; `audio_task` and `bt_task` call `gui_set_fatal_error()` on failure.

**Remove** (Proposed, in this order; build after each step):

| Remove | Notes |
|---|---|
| `gui_task.*`, `components/gui`, `gui_assets`, `lvgl`, `lvgl_drivers` | Also the `EXTRA_COMPONENT_DIRS` line, `main/CMakeLists.txt` `REQUIRES` and `LV_CONF_INCLUDE_SIMPLE`, and 109 `CONFIG_LV_*` lines in `sdkconfig`. |
| `gcore_task.*`, `components/gcore` (except what replaces `ps`) | gcore, power, RTC and time utilities. |
| `sample.c`, `AUDIO_SAMPLE_ENABLE`, `SCREENDUMP_ENABLE`, `fatfs` | The SD-card audio dump. |
| Caller ID: `adsi`, `fsk`, `crc`, `queue`, `async` spandsp files, the CID state machine in `pots_task.c`, RP-AS and line reverse | A rotary phone has no display. |
| DTMF receive (`dtmf`, `super_tone_rx`, `tone_detect`) | Rotary dial only. |
| `international.c` country table and `audio_assets/*` | Replaced by the era profiles from `tones.md`. |
| `ps.c` | Replaced by NVS (see above). |

Let the compiler and linker show what is left over. Commit or stash the dirty `sdkconfig` and
`dependencies.lock` first. `app_task.c` needs its GUI and gcore hooks removed (GUI digit and
dial-button notifications, gain notifications, activity pings, status updates). `bt_task.c` needs its
GUI notifications and gain plumbing removed.

**Change:**
- **Pins** (see Hardware platform), and confirm the codec chip.
- **`audio_task`:** remove the `rx * -1` AG1171 inversion; select `LINE2` for both input and output in the
  codec config (line-in = LIN2/RIN2, headphone = LOUT2/ROUT2); replace `gui_set_fatal_error` with a log.
  **Proposed:** drop the line echo canceller (it existed for the SLIC hybrid). If dropped, also
  remove `esp_hf_client_send_nrec()` in `_btSetState`, which tells the cellphone to disable its own
  echo cancellation.
- **`pots_task`:** replace the single hook/pulse line with separate GPIOs (hook, dial pulse,
  dial-in-progress). This removes the `ON_HOOK_PROVISIONAL` state and its 500 ms wait, which existed
  only to tell pulses from a hangup. Replace `_potsLineReverse` / `_potsLineRingMode` with the
  DRV8825 STEP/ENABLE/DIR control. The dial-in-progress contact also gives a clean end-of-digit
  signal instead of the 100 ms make timeout.
- **`app_task`:** add the mode step in the dialing state, the 10-digit rule, and the mode states.
  Note an existing upstream bug at `app_task.c:569` (`} if (!bt_in_call)` where an `else` was
  probably meant); it only matters for the GUI dial button, so it goes away with the removal.

**Add:** the modes above, the volume and era persistence, one-shot clip playback, the era tone
profiles (wavetable dial tone, cadence gating for busy and ringback), local ring cadence, and pairing
via passkey entry.

**Suggested order:** (1) strip the GUI and gcore, replace `ps`, and get a clean build that boots and
pairs on the ELEGOO board with `bt_task` alone; (2) bring up the A1S codec and audio path; (3) new
pots hardware layer; (4) modal dial and pairing; (5) tones, clips, volume and era modes; (6) ring
driver.

## Toolchain / build notes

- ESP-IDF v4.4.4 (pinned by this repo's `sdkconfig`/`dependencies.lock`), confirmed
  building successfully with two environment workarounds needed for a modern host:
  - `pip install "setuptools<81"` in the IDF Python venv — newer setuptools removed
    `pkg_resources`, which IDF 4.4.4's tooling still imports directly.
  - `export CMAKE_POLICY_VERSION_MINIMUM=3.5` — modern CMake (4.x) dropped support
    for the old `cmake_minimum_required` versions IDF 4.4.4's bundled `mbedtls`
    declares; this environment variable is the documented escape hatch.
- Target chip: plain `esp32` (Xtensa), matches both the ELEGOO dev boards and the
  ESP32-A1S module.

## Parts list (bill of materials)

| Part | Role | Status |
|---|---|---|
| ESP32-A1S "Audio Kit" board (ESP32 + onboard codec + battery mgmt) | Main MCU, audio codec, battery charge/boost | Candidate identified; **codec chip (ES8388 vs AC101) unconfirmed** |
| MAX9814 electret mic + AGC breakout (5-pin: VCC/GND/OUT/GAIN/AR) | Handset microphone | Confirmed part |
| DRV8825 stepper driver module | Ring (bell) driver, run in "dumb" voltage-mode | Confirmed choice; specific listing not yet pinned down |
| XL6019-family adjustable boost module (3-35V→5-40V, ~5A) × 2 | Ring-driver power: stage 1 (battery→~12V) and stage 2 (~12V→bench-tuned voltage) | Confirmed part, ×2 needed |
| 3.7V LiPo battery (capacity TBD), JST-XH connector | Main system power | Not yet chosen — must match ESP32-A1S board's BAT+/BAT- connector |
| Coiled 3-conductor (TRS) headphone extension cable | Handset cord: mic-out, speaker-signal, shared ground | Already on hand (user-supplied) |
| Donor phone's rotary dial mechanism | Dialing input | Salvaged from donor phone |
| Donor phone's bell ringer (2× ~2000Ω coils, gongs, clapper) | Ring output | Salvaged from donor phone |
| Speaker/earpiece | Audio output to handset | **Open — reuse original receiver (pending impedance check) or replace** |
| External pull-up resistor on DRV8825 nENABLE | Keep the bell off during ESP32 reset/boot | Proposed |

**Owned, not part of the final build**:
- ELEGOO EL-SM-012 bare ESP32-WROOM-32 boards (3-pack) — verified BT Classic/HFP-capable, but lack the onboard codec/battery management the ESP32-A1S provides; kept as spares/dev boards.
- Raspberry Pi Pico Probe — confirmed not usable for ESP32 (Xtensa) flashing/debugging via probe-rs; irrelevant to this project's toolchain (UART flashing only).

**Considered and ruled out** (not part of the BOM, kept here so they don't get re-litigated):
- nice!nano (nRF52840) as the Bluetooth link — BLE-only, no Classic BT/HFP support.
- AG1171 SLIC / weeBell-style hybrid — unnecessary once the carbon mic/network aren't preserved.
- BTS7960/IBT-2 H-bridge module — 27V ceiling, monolithic (no MOSFET-swap path).
- NOYITO/generic TO-220-MOSFET H-bridge boards — viable but shelved in favor of DRV8825 once the ~36V reference-build data point emerged.
- Reversed mains/doorbell transformers, EL-wire inverters, industrial enclosed stepper drivers (DM556/DM860), 2×DRV8825 series-stacking — all addressed under Ring Driver above.

## Open items

- Confirm ES8388 vs AC101 on the actual ESP32-A1S board received, and the real pin map.
- Bench-test ring driver: actual frequency (mechanical resonance sweep) and actual
  voltage-vs-loudness curve for this specific bell, before finalizing the two-stage
  boost converter's target voltage. Confirm the STEP-to-output-frequency ratio.
- Confirm original receiver (earpiece) impedance to decide whether it can be reused
  driven from the codec's output, or needs replacement.
- Audit `app_task`'s on-hook handling to confirm it correctly unwinds any in-progress mode (passkey
  entry, forget-pairing confirmation), not just call states.
- Volume ranges: tune the wide starting ranges on the bench (sec. "Volume model").
- Tune the era timeouts (about 50 s off-hook and about 16 s partial-dial for the US Precise era).
- Whether to keep a "dial `0` for voice assistant" behavior, and if so on which digit.
- UK era: confirm its ringback tone (currently a flagged placeholder) and its off-hook and
  partial-dial behavior (`tones.md`, section 10).
- Identify the codec (ES8388 vs AC101) inside the A1S module and pick free GPIOs from the header.
- Whether to mute the cellphone's own ringback and play the period tone instead.
- Voice and style for the recorded prompts, and the clip sample rate and format (`audio_clips.md`; SD card storage is decided).
- GPIO budget with the SD card: which fifth GPIO (or which input to drop) — validation plan stage 4.
- Verify tone levels and modulation depth by ear or by analysing recordings (`tones.md`).
