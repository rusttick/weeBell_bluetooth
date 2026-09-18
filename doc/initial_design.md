# Rotary Phone → Bluetooth Handsfree: Initial Design

Follow-on from `doc/initial_research.md`. That doc recommended "ESP32 + weeBell-style
SLIC/codec board, Bluetooth HFP to your cellphone" as the pragmatic path. This doc
captures where the design actually landed after working through the hardware and
firmware in detail — which turns out to be simpler than a full weeBell/SLIC build,
because we're only salvaging two mechanical parts from the donor phone rather than
the whole electrical network.

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
- Battery: onboard JST-XH BAT+/BAT- connector for a 3.7V LiPo, onboard charging
  circuit, onboard boost converter to the ~5V rail the board itself needs, with
  pass-through charge-while-powered operation. This is a separate power domain from
  the ring-voltage boost chain (below) — the board's 5V rail is not expected to
  supply the ring driver.
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
  output frequency.
- ENABLE/DIR/STEP are plain 3.3V logic, direct from ESP32 GPIO/timer/PWM.
- A ~2µF non-polarized series capacitor sits between one driver output and the bell
  coil. Not needed for DC-blocking in the SLIC-free sense (no shared line to isolate
  from), but kept as cheap fault-tolerance insurance: it guarantees a firmware
  fault (stuck GPIO, crash mid-cycle) can't leave sustained DC across the coil.
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

## Control interface — no touchscreen

The original gCore/LVGL touchscreen UI is being removed entirely. Everything the
screen used to do gets replaced by the phone itself:

- `components/gui`, `components/gui_assets`, `components/lvgl*`, `gui_task.c/h`, and
  the display init in `main.c` get deleted.
- **Locale** (country tone/ring/CID format selection) becomes a compile-time
  `#define`, not a runtime menu.
- **Gain** (mic/speaker levels) gets tuned once by hand — either a hardcoded constant
  (recompile until it sounds right) or a physical trim potentiometer on the analog
  path — no runtime UI needed.
- **RTC/time** is dropped entirely. The only place time was used was the
  `CLIP_DATETIME` field in outgoing Caller ID messages (`pots_task.c`) — it doesn't
  affect calls, dialing, or audio. A hardcoded/dummy timestamp is fine.
- **Bluetooth pairing, forget-pairing, mute/DND, and any other former GUI-only
  functions** move to the modal dial interface below.

### Modal dial interface

Replaces the on-screen keypad and settings menus. After the handset goes off-hook:

- The **first digit dialed selects a mode**.
  - `1` → normal outgoing call. Behaves like current firmware's dialing logic, except
    **no fixed inter-digit timeout** — collects digits until a full number (up to 10
    digits) is entered, using only a short (sub-1-second) debounce/pause to detect
    "digit finished," not the current 4-second wait-and-see. The call is placed as
    soon as the number is complete, since the mode is already disambiguated by the
    leading `1`.
  - `2`-`8` → reserved for special modes (Bluetooth pairing/forget-pairing is the
    leading candidate for one of these; others not yet assigned).
  - `0` → reserved, not yet assigned a mode.
- **Once a mode is selected, digits 0-9 are all ordinary numeric input for that
  mode** — `0` has no special/global meaning inside a mode; its behavior is entirely
  mode-specific.
- **Going on-hook (hangup) unconditionally cancels whatever mode is active** and
  returns to idle. This needs to be audited against the existing `app_task` state
  machine once implemented — the on-hook handler currently resets call-related
  states; it needs to also cleanly unwind any in-progress special-mode state (e.g.
  mid-passkey-entry), not just call states.

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
2. On `ESP_BT_GAP_KEY_REQ_EVT` (currently just logged, `bt_task.c` ~line 411), enter
   a "collecting passkey" state and route the next 6 dialed digits there instead of
   into the normal number buffer.
3. Once 6 digits are collected, call `esp_bt_gap_ssp_passkey_reply(bd_addr, true, passkey)`.
4. Use the existing tone generator for pairing feedback (enter-pairing-mode,
   success, failure, forget-pairing-confirmed) in place of on-screen status, since
   there's no display.

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
| ~2µF non-polarized series capacitor (voltage rating above bus, e.g. ≥50V) | Fault-tolerance isolation between ring driver and bell coils | Spec'd, specific part not yet chosen |
| 3.7V LiPo battery (capacity TBD), JST-XH connector | Main system power | Not yet chosen — must match ESP32-A1S board's BAT+/BAT- connector |
| Coiled 3-conductor (TRS) headphone extension cable | Handset cord: mic-out, speaker-signal, shared ground | Already on hand (user-supplied) |
| Donor phone's rotary dial mechanism | Dialing input | Salvaged from donor phone |
| Donor phone's bell ringer (2× ~2000Ω coils, gongs, clapper) | Ring output | Salvaged from donor phone |
| Speaker/earpiece | Audio output to handset | **Open — reuse original receiver (pending impedance check) or replace** |

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

- Confirm ES8388 vs AC101 on the actual ESP32-A1S board received.
- Bench-test ring driver: actual frequency (mechanical resonance sweep) and actual
  voltage-vs-loudness curve for this specific bell, before finalizing the two-stage
  boost converter's target voltage.
- Assign functions to dial-modes `2`-`8` and `0` (pairing/forget-pairing is the only
  one currently planned; DND/mute/volume-trim equivalents, if wanted, need a home).
- Confirm original receiver (earpiece) impedance to decide whether it can be reused
  driven from the codec's output, or needs replacement.
- Audit `app_task`'s on-hook handling to confirm it correctly unwinds any
  in-progress special dial-mode, not just call states.
