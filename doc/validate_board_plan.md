# Board Validation Plan

Step-by-step plan to prove that the ESP32 Audio Kit V2.2 board (`doc/audio_kit_v2.2.md`) can do everything
this project needs, starting from "can I flash it at all?" and finishing when every needed board and chip
feature is confirmed working. Companion to `doc/initial_design.md`. Written 2026-09-18.

**Nothing in this plan has been run yet.** It is a plan, not results. Record results in a log as you go (a
table template is at the end).

## How to use this plan

- **Do the stages in order.** Each one depends on the one before. Stop at the first failure and fix it
  before moving on; the "If it fails" notes say where to look.
- **Change one thing at a time**, and note what you changed.
- **Use a separate test project** for stages 1–8, not the main firmware. Suggested: a small ESP-IDF 4.4.4
  project in the repo (for example `hwtest/`) with a serial console that has one command per test
  (`i2cscan`, `gpio`, `tone`, `sd`, and so on). One flash then covers many tests. The main firmware
  (`gcore_pots_bt/`) is not touched until the board is proven.
- **Tools needed:** the board, a **data-capable** micro-USB cable, a multimeter, a few jumper wires and
  10 kΩ resistors, a pair of headphones, a signal source with a 3.5 mm output (a phone playing a tone is
  fine), a microSD card (FAT32, 32 GB or smaller for the first tests). **Nice to have:** an oscilloscope or
  a cheap logic analyzer, a USB audio interface or a phone to record the output.
- **Assumptions to check, not trust:** the codec variant (I2C on 18/23 vs 33/32), the GPIO plan, and the
  claims in `audio_kit_v2.2.md` marked inferred or reported.

## Stage overview

| # | Stage | Question it answers |
|---|---|---|
| 0 | Preparation | Is everything ready and safe? |
| 1 | USB serial and flashing | Can we talk to the chip and flash it? |
| 2 | Hello world every second | Does our own code run, and can we see its output? |
| 3 | Identify the module | Which codec/pin map do we have? Does PSRAM work? |
| 4 | GPIOs and switches | Which pins can we really use? |
| 5 | Codec control | Can we talk to the ES8388? |
| 6 | Audio output | Does the earpiece path work, and do volume steps behave? |
| 7 | Audio input | Does the microphone path work? |
| 8 | SD card | Can we store and play many high-resolution prompts? |
| 9 | Bluetooth handsfree | Can it pair, call, and pass voice audio? |
| 10 | Dial and hook inputs | Can we read the real phone's contacts? |
| 11 | Ring driver | Does the bell driver work safely? |
| 12 | Power | Do battery, USB and brown-out behave? |
| 13 | Integration soak | Does everything work together for hours? |

---

## Stage 0 — Preparation

**Goal:** a known starting point, so later results mean something.

**DIP:** all OFF.

1. **Photograph** the board top and bottom, the shield can, and the DIP switches. Note everything printed,
   including "a618" and "k547".
2. **Set the DIP switches all OFF.** Leave the SD slot **empty** and the **battery disconnected**. Connect
   nothing to any header.
3. **Toolchain:** ESP-IDF v4.4.4 with the two workarounds in `initial_design.md` (`setuptools<81` in the
   IDF Python environment; `export CMAKE_POLICY_VERSION_MINIMUM=3.5`). Confirm `idf.py --version` prints
   v4.4.4.
4. **Serial driver:** recent macOS includes a driver for the CP2102. If no port appears in stage 1,
   install Silicon Labs' CP210x driver.
5. **Power:** use only the micro-USB labeled **UART/POWER**. (The other port is 5 V only.)

**Pass:** you can run `idf.py --version` and you have a photo record of the board.

---

## Stage 1 — Verify the flashing method

**Goal:** prove USB serial and the bootloader connection work, with no code of ours involved.

**DIP:** all OFF.

1. **Find the port.** Run `ls /dev/tty.*` with the board unplugged, then plugged in. A new
   `/dev/tty.usbserial-*` (or `/dev/cu.SLAB_USBtoUART`) appears. Also check `system_profiler SPUSBDataType`
   shows a "CP2102 USB to UART Bridge".
   *If nothing appears:* try another cable (many cables are charge-only) and another USB port, then the
   driver (stage 0, step 4).
2. **Check the 3.3 V rail** with the multimeter on the header's 3V3 and GND pins. Expect about **3.38 V**
   (the schematic's regulator note). **Fail** if it is below 3.2 V or above 3.6 V: the module is rated
   3.0–3.6 V.
3. **Read the chip** without flashing anything:
   - `esptool.py --port <port> chip_id`
   - `esptool.py --port <port> flash_id`
   - `esptool.py --port <port> read_mac`
   Expect: an **ESP32** (dual core, 240 MHz, Wi-Fi + BT), a flash chip, and a **4 MB** flash size. Write
   down the **chip revision** (the newest ESP-IDF firmware here requires revision 3 or later; see stage 2),
   the flash manufacturer/device ID, the MAC address and the crystal frequency (40 MHz).
4. **Test the auto-reset.** The board has a DTR/RTS reset circuit. If `chip_id` connects on its own, it
   works. If it fails with "Failed to connect," use the manual method: **hold BOOT, tap RESET, release
   BOOT**, then retry. Note which method is needed.
5. **Read (only read) the eFuses:** `espefuse.py --port <port> summary`. Write down the flash-voltage
   settings and any "VDD_SDIO" force bits. This matters for the SD card stage because the SD DATA2 line
   is on GPIO12, the strapping pin that selects flash voltage. **Do not burn any eFuse.**
6. **Test baud rates:** repeat `chip_id` with `--baud 115200`, then `921600`. Note the fastest reliable one.

**Pass:** chip ID, MAC and 4 MB flash reported, auto-reset or manual method identified, eFuses recorded.
**If it fails:** cable, driver, USB port, then the manual bootloader method; if the chip reports garbage,
check the 3.3 V rail.

---

## Stage 2 — Hello world, once per second

**Goal:** our own code builds, flashes and runs, and we can see its output.

**DIP:** all OFF.

1. **Create a minimal project** (`hwtest/`): `app_main` prints a message with a counter and the uptime once
   per second.
2. **Set it up for this board**, which differs from the main firmware's `sdkconfig`:
   - **Flash size 4 MB** (the main firmware's `sdkconfig` says 16 MB, which does not match this board).
   - **Console on UART0 at 115200.**
   - **Minimum chip revision:** the main firmware requires revision 3 or later (`CONFIG_ESP32_REV_MIN_3`).
     If stage 1 showed an older revision, set the minimum to match, or the app will refuse to start.
   - **PSRAM off for now** (stage 3 turns it on).
   - The main firmware also sets the bootloader's VDD_SDIO boost to 1.9 V and flash mode to QIO at 80 MHz;
     start with the ESP-IDF defaults (DIO, 40 MHz) and only change them if needed.
3. **Build, flash and monitor:** `idf.py set-target esp32`, `idf.py build`,
   `idf.py -p <port> flash monitor`. Exit the monitor with Ctrl+].
4. **Expect:** the boot log (chip revision, flash size, partition table), then one line per second.
5. **Check the reset behavior:** press RESET; the counter restarts. Note the reset reason printed.
6. **Stress the link:** raise the print rate to 100 lines per second for a minute, and confirm nothing is
   dropped or garbled.

**About "RTT":** RTT is a SEGGER debug-probe feature and **does not exist for the ESP32**. The equivalents:
- **UART console through the CP2102 (used here, and enough for this whole plan).** It is already on the
  board and needs no extra hardware.
- **ESP-IDF application trace over JTAG** needs a separate JTAG probe (for example an ESP-Prog) connected
  to the JTAG header. The header shares pins we may want for GPIOs (stage 4), so it is optional and
  deferred.
- **Network logging** (UDP over Wi-Fi) is possible, but Wi-Fi is not otherwise used in this project.

**Pass:** one line per second for a few minutes, clean boot log, reset works.
**If it fails:** wrong flash size or chip revision (the boot log says so), wrong baud rate, or the
bootloader failing to find the partition table.

---

## Stage 3 — Identify the module and memory

**Goal:** know exactly which board variant we have, and confirm PSRAM.

**DIP:** all OFF.

1. **Log the chip and flash info** from the boot log and the firmware (`esp_chip_info`, flash size).
2. **Enable PSRAM** (64 Mbit, auto-detect) and print the chip size (`esp_spiram_get_size()`) and
   `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`. Expect a chip size of **8 MB** but a heap of about
   **4 MB**: the ESP32 maps only 4 MB of external RAM, and the rest needs himem (bank switching). 4 MB is
   ample for this project. **Fail** if PSRAM init errors: check the mode setting and note the message.
3. **Scan I2C on both candidate pin pairs** (leave everything else idle). Do **not** drive GPIO0.
   - Pair A: **SDA 18 / SCL 23**
   - Pair B: **SDA 33 / SCL 32**
   The ES8388 answers at **0x10**. Record which pair answered. **This decides the whole pin plan.** The
   newest module answers on pair A; older ones on pair B.
   *If neither answers:* check that GPIO0 is left alone, the module 3.3 V, and try the AC101's address
   **0x1A**.
4. **Record** the results in the log, and update `audio_kit_v2.2.md` (sections 4 and 9).

**Pass:** PSRAM chip is 8 MB with about 4 MB in the heap and a clean read-back test, and one I2C pair shows a device at 0x10 (or 0x1A).
**Decision it feeds:** newest vs. older module; whether GPIO 5, 18 and 23 are free.

---

## Stage 4 — GPIOs and switches

**Goal:** find out which GPIOs we can actually use, given the board's wiring and the SD card requirement.

**DIP:** SW4 ON (IO13 to the P1 MTCK pin), rest OFF; flip others only for the continuity checks.

**Tool:** the `hwtest` console commands `boot`, `pins`, `mode`, `set` and `watch` (see `hwtest/README.md`).
Flash it as in stage 2. Only IO13/MTCK, IO18, IO23, IO22, IO19, IO5 and IO21 can be touched. **KEY3 (IO19) is
broken open**, so IO19 is tested with a jumper wire to GND. IO21 (speaker-amp enable) is tested in stage 6.

**The SD card changes the GPIO plan** (recorded in `initial_design.md`). In 1-bit mode the SD slot needs
**IO14 (CLK), IO15 (CMD) and IO2 (DATA0)**; 4-bit mode also needs IO4, IO12 and IO13. The proposal in
`audio_kit_v2.2.md` section 6 used IO13, IO14 and IO15 as inputs, which now **conflicts** with the SD card.

1. **Multimeter, power off:**
   - Measure the **resistance from GND to each header GPIO** (IO19, 23, 18, 5, 22, 21) and to the four P1 pins
     (MTCK, MTDO, MTMS, MTDI). Note anything unexpected. **An ohmmeter on a MΩ range charges a floating pin above
     3.3 V,** so take voltage readings first, or press the pin's key or reset the board afterward.
   - Confirm the DIP switches: with a beeper or ohmmeter, verify what each switch connects (from the spec:
     SW1 IO13–KEY2, SW2 IO13–DATA3, SW3 IO15–CMD, SW4 IO13–MTCK, SW5 IO15–MTDO).
2. **Idle voltages, USB powered:** read each of those pins to GND.
3. **Starting levels:** press RESET, then run `boot` and `pins` with nothing touched. Record every level.
   Expect IO13/MTCK = 0 (a 10 kΩ pull-down, probably R15). IO5, IO18 and IO23 have no pull-up on this board:
   they float, and `watch` on them alone scrolls endlessly with mains hum.
4. **Inputs and keys:**
   - `mode 5 pu`, `mode 18 pu`, `mode 23 pu`, `mode 19 pu`; `pins` should show all four at 1.
   - `watch 13 5 18 23 19`. Press and release KEY4 (IO23), KEY5 (IO18) and KEY6 (IO5). Each should print `-> 0`
     on press and `-> 1` on release, and only that pin should change (**the keys must be decoupled from each
     other**). KEY2 reaches IO13 only through DIP SW1, which stays OFF, so it is not part of this test. Touch a
     jumper from IO19 to GND and remove it.
   - **Hook input:** jumper a **3V3** pin to **MTCK**. IO13/MTCK should read 1, and 0 when removed.
   - Note any contact bounce (several edges a few hundred microseconds apart), then `watch off`.
5. **Outputs:** `mode <pin> in` on the inputs above. Then `mode 22 out 1`, `set 22 0`, `set 22 1` and measure
   IO22 to GND each time (about 3.3 V and 0 V). **LED4 lights when IO22 is low.** Repeat for IO19 with **LED5**.
   `set` prints how long the pad took to follow the new level; a slow or stuck pad means a load or something
   holding the pin (IO19 shares its pin with the key ladder).
   Return both with `mode 22 in` and `mode 19 in`.
6. **Boot-state check (bell safety), optional:** an accidental ring is audible and easy to patch, and nENABLE has its own
   external pull-up, so this may be skipped; stage 11 step 2 checks it with the real driver. With a meter or scope on IO22 and IO19 (the STEP and nENABLE pins), press
   RESET and unplug and replug USB. Nothing should glitch, and nothing should toggle before firmware starts.
   Without a scope, watch LED4 and LED5: a low pulse lights them, but a brief high glitch is invisible, so use a
   scope for that if you have one. `boot` after the reset should match step 3.
7. **Work out the GPIO budget with the SD card.** We need **3 inputs** (hook, dial pulse, dial-in-progress)
   and **3 outputs** (STEP, nENABLE, speaker-amp enable), and the SD slot takes IO14, IO15, IO2. Candidates that remain:

   | Candidate | How it becomes usable | Note |
   |---|---|---|
   | **IO13** | P1 **MTCK** pin with DIP SW4 ON; SW1 and SW2 OFF | measured 10 kΩ to GND: **hook contact between 3V3 and MTCK, active-high, no external pull-up, leave the pull-down in place**. Keep SW2 OFF so DATA3 stays pulled up on the card side |
   | **IO22** | free (LED4) | output |
   | **IO19** | free (LED5 follows it) | output, **slow**: the pad takes about 38 µs to follow a change (capacitance from the key ladder). Use it only for the static nENABLE, never for STEP. KEY3 is broken, so no key interference |
   | **IO18, IO23** | free on this **older** module (stage 3); the key ladder gives no pull-up | inputs, need a pull-up (external 4.7–10 kΩ, or internal) |
   | **IO21** | free output; R46 links it to the speaker-amp enable, R51 holds that low | **speaker-amp enable**: the earpiece is on the speaker outputs, so the amps must be switched on (high) while audio plays |
   | **IO5** | strapping pin | avoid; spare |
   | **IO12** | P1 **MTDI** pin | strapping pin (flash voltage); avoid |

   That gives the six signals without changing the board: hook on IO13/MTCK, dial pulse on IO18,
   dial-in-progress on IO23, STEP on IO22, nENABLE on IO19, speaker-amp enable on IO21. **Decide here** and
   update the design.

**Pass:** a documented, tested list of GPIOs for hook, pulse, dial-in-progress, STEP, nENABLE and the amp enable that
works with the SD card, keys decoupled, and the boot-state check clean.

---

## Stage 5 — Codec control

**Goal:** talk to the ES8388 over I2C, with no audio yet.

**DIP:** all OFF.

**Tool:** the `hwtest` command `codec` (`regs`, `rw`, `init`). The repo's `es8388.c` cannot be used as is (it is
tied to the gCore board's I2C pins 21/22 and to `audio_hal`), so `hwtest/main/cmds_codec.c` repeats its
`es8388_init()` register sequence over our own I2C bus: **SDA 33 / SCL 32** (stage 3), 100 kHz, address 0x10, I2S
slave mode, `adc_input = LINE2`, `dac_output = LINE2` (line-in and headphone on this board). It enables no
internal pull-ups, so a pass shows the module's own 10 kΩ pull-ups are enough.

1. **Power-on registers:** `codec regs` after RESET. All 53 registers should read with no errors. Record them.
2. **Read/write test:** `codec rw` writes six patterns to the two DAC volume registers (0x1a, 0x1b), reads each back
   and restores the original. Expect all patterns to match.
3. **Init:** `codec init` resets the codec, runs the init sequence and reads every register back. Expect no I2C
   errors. A few registers may read back different from what was written (reserved bits); note which.
4. **After init:** `codec regs` again and record it.
5. **No audio yet.** The codec draws its clock from **MCLK on GPIO0**, so I2S must be running before audio works
   (stage 6). Confirm nothing crashes and the console stays responsive.

**Pass:** register write/read-back works and the init completes.
**If it fails:** wrong I2C pins (stage 3), pull-ups (the module has 10 kΩ pull-ups already; try `pu` on the pins), or
GPIO0 being driven by something else.

---

## Stage 6 — Audio output (earpiece path)

**Goal:** a clean tone from the earpiece path, and volume steps that behave. **The earpiece is on the speaker
outputs (J3 and J4):** the codec's LOUT1/ROUT1 go through two class-D amplifiers, which are switched on by **IO21
high**. The headphone jack (LOUT2/ROUT2) is used as a bench check.

**DIP:** all OFF.

**What the test firmware does:** the `hwtest` commands `audio`, `tone`, `vol`, `volstep` and `mute` play a sine wave
through the codec. The ESP32 supplies the codec's clock (**MCLK on GPIO0**) and the I2S signals (BCLK 27, WS 25,
data out 26). `audio on` starts them and runs the stage 5 codec setup. `audio out hp|spk|both` chooses the
outputs: `hp` = headphone jack (the default), `spk` = speaker outputs plus IO21 high, `both` = both. The sine is
-1 dBFS by default (`tone <Hz> <dBFS>` changes it). `vol` sets the output gain in dB, from -91.5 (silent) to +4.5 (loudest); the main firmware's earpiece range
is -43.5 to +4.5. **The gain starts at -40 dB, which is quiet. Keep your ear away from the headphones and earpiece
whenever you change the level, the sample rate or the output.**

**Speaker-output warning.** Each speaker output is a *bridge* (two pins, + and −, per channel), and **neither pin is
ground**. Never connect either pin to ground or to a shared ground wire: the amplifier can be damaged. A vintage
earpiece is not a 3 W speaker; start at a very low level and raise it slowly.

### 1. Tone on the headphone jack (bench check)

Plug headphones into the **headphone** jack, flash `hwtest` (see `hwtest/README.md`), and wait for `hwtest>`.
Type each line, one at a time.

| Type | What should happen |
|---|---|
| `audio on 8000` | Prints `audio on: 8000 Hz, ...` and `init completed`. No sound yet. |
| `tone 1000` | Prints `tone 1000 Hz`. A very quiet 1 kHz tone starts (the gain is -40 dB). |
| `vol -30` | A little louder. |
| `vol -20` | Louder again. Stop at a comfortable level. |

**Pass:** a clean, steady 1 kHz tone at equal volume in both ears, louder with each `vol` step. (Done 2026-09-19.)

**If it goes wrong**
- **Only one ear:** type `codec r 0x04`; it must read `0x0c`. If not, `codec w 0x04 0x0c`. (The repo's `es8388.c`
  value 0x28 enables only the left channel; already fixed in `hwtest`.)
- **Silent:** type `codec regs` and check 0x04 = 0x0c, 0x19 = 0x60 (0x64 means muted: `mute off`), 0x2e to 0x31 =
  0x21, and 0x1a and 0x1b below 0xc0. Also `audio status`.
- **Still silent:** the BCLK pin may be the newer-module one. Reset, type `audio pins 5 25 26`, and repeat. A scope
  on GPIO0 shows whether MCLK is running.

### 2. Speaker outputs: the earpiece path

Connect a small test speaker (4 to 8 Ω) or the earpiece across the **+ and − pins of J3** (left) or J4 (right).
Nothing to ground. Then:

| Type | What should happen |
|---|---|
| `audio out spk` | Prints `output: speaker outputs (J3/J4)`. IO21 goes high (amps on); the headphone jack goes silent. |
| `vol -40` | Very quiet. |
| `tone 1000` | A quiet 1 kHz tone from the speaker or earpiece. |
| `vol -30`, then `vol -20` | Louder in steps. Stop well before it is loud. |

Also type `pins`: IO21 should read 1. Then `audio out hp`: the speaker should go silent and IO21 should read 0.

**Pass:** a clean tone from the speaker outputs, and silence when they are switched off.

### 3. Amps off when idle

Type `audio off`. The speaker must be silent and `pins` must show IO21 = 0. Press RESET: the speaker must stay silent
at boot (no pop or hiss).

### 4. Sample rates

On the speaker path (`audio out spk`), for each rate type the line, then `tone 1000`, then listen:

`audio on 16000`, `audio on 22050`, `audio on 44100`

Note which rates work and which sound clean. (The existing firmware runs the codec at 8 kHz; recorded prompts may
want a higher rate, see stage 8.)

### 5. Volume steps

The design has 10 volume levels on the dial, digit 1 quietest and digit 0 loudest, evenly spaced in dB. With a tone
playing on the earpiece path (`audio on 8000`, `audio out spk`, `tone 1000`), type `volstep 1`, then `volstep 2`,
and so on to `volstep 9`, then `volstep 0`. Each prints the step and the gain in dB and changes the volume.

For each step, write down what you hear or measure (hold a phone recorder near the earpiece, or use an audio
interface, and note the amplitude). Then check:
- the steps sound **evenly spaced** (each about the same jump),
- step 1 is still **audible**,
- step 0 does **not clip or distort**, and is not painfully loud.

If the low steps are too quiet or the top step is too loud or distorts, narrow the range with
`volstep <digit> <min_dB> <max_dB>`, for example `volstep 1 -30 -5`, and note the min and max that sound right.
Those become the final range constants.

| Digit | Gain (dB) | What I heard or measured |
|---|---|---|
| 1 | | |
| 2 | | |
| 3 | | |
| 4 | | |
| 5 | | |
| 6 | | |
| 7 | | |
| 8 | | |
| 9 | | |
| 0 | | |

### 6. Frequency response and level (earpiece)

**Skipped (2026-09-20):** level differences between tones cannot be judged accurately by ear. Revisit with real Bluetooth
audio in stage 9 if the earpiece sounds harsh; the `eq` command and the capacitor options below are ready.

**Judge the level at 1 kHz, not at low tones.** A small earpiece and the ear are both far less sensitive at 40 Hz than
at 1 kHz (the difference is tens of dB), so a level that is just audible at 40 Hz will be very loud at 1 kHz. The
phone carries speech from about 300 Hz to 3.4 kHz; a 1 kHz tone is the telephone reference.

1. Set the pad resistor (step 7, noise) so that `tone 1000` at `vol 4.5` is the loudest level you would ever want.
   With 820 Ω in each leg (about -52 dB) 1 kHz was still too loud: try 1.5 kΩ (-57.5 dB), 2.2 kΩ (-60.8 dB) or
   4.7 kΩ (-67.4 dB).
2. Then sweep `tone 300`, `tone 500`, `tone 1000`, `tone 2000`, `tone 3000` at the same `vol` and note how the level
   changes. A small earpiece is usually loudest and harshest in the 1 to 3 kHz range.
3. To tame the highs, use the software filter `eq`: `eq lp 3000` (low-pass at 3 kHz) or `eq hs 1500 -6` (6 dB less
   above 1.5 kHz). It prints the response it will give. Repeat the sweep and listen; use `eq off` to compare. Note the
   `eq` setting that sounds best: it becomes a filter in the firmware's playback path (8 kHz speech; the filter costs
   almost nothing there).

**Hardware filters with a capacitor** (numbers for 820 Ω in each leg and a 4 Ω earpiece; the corners move if the
pad resistors change):
- **In series with the earpiece** it forms a high-pass with the total series resistance (about 1644 Ω): corner
  `1 / (2π · 1644 · C)`. 3300 µF does nothing (0.03 Hz). A **film or ceramic** 1 µF gives 97 Hz (60 Hz -5.6 dB),
  0.47 µF gives 206 Hz (-11 dB), **0.33 µF gives 293 Hz (60 Hz -14 dB, 300 Hz -2.9 dB, 1 kHz -0.4 dB)**, 0.1 µF gives
  968 Hz. This cuts hum and rumble below the speech band.
- **Across the earpiece** it forms a low-pass with the earpiece's 4 Ω: corner `1 / (2π · 4 · C)`. 3300 µF gives
  12 Hz, which kills everything above bass (1 kHz -38 dB). For a gentle treble cut use 10 µF (corner 4 kHz, 3 kHz
  -2 dB) or 22 µF (1.8 kHz, 3 kHz -5.7 dB), **non-polar** (film, or two electrolytics back to back).
- An electrolytic such as the 3300 µF is polarized and is meant for DC bias, so it is the wrong part for an AC audio
  signal in any case. The software `eq` gives the same results without soldering: `eq hp 300` and `eq lp 3000`.

### 7. Noise and pops

- **Hiss or hum:** type `tone off` (silence with audio on). Listen for hiss or hum from the earpiece.
- **Pops:** type `mute on` then `mute off`, `audio out hp` then `audio out spk`, and `audio off` then
  `audio on 8000`. Listen for clicks or pops, especially when the amps switch on.

**Pass:** clean tone at the working sample rates, even volume steps, silence when the amps are off, no bad noise
or pops.

**If the noise floor is too high.** Measured 2026-09-19 on the speaker outputs: the hiss does **not** change with
`mute on` or with `vol -80` versus `vol -10`. So it is added after the digital signal and the volume control, by
the codec's analog output stage or by the amplifier, and no digital or volume setting can remove it. What decides
how audible it is: the signal-to-noise ratio at the earpiece, which is the maximum clean signal divided by that
fixed hiss.

1. **Give the amplifier the biggest clean signal.** Run the tone near full scale (`tone 1000` now defaults to -1
   dBFS) and the volume at its top (`vol 4.5`), and check it does not distort. The main firmware must do the same:
   use the full range of the codec, and never leave the audio quiet in the digital domain and turn up the analog gain.
2. **The amplifier has far more gain than the earpiece needs, so reduce the loudness after the codec, not before it.**
   Quieting the codec makes the signal smaller under the same hiss. Two hardware options, best first:
   - **Resistor pad after the amplifier.** Series resistors in both legs (never to ground). It lowers the signal and
     the hiss together, so the codec can be driven at full scale for the same listening level. Attenuation is
     `Z / (Z + 2R)` for an earpiece of impedance Z and R in each leg. **Our earpiece is 3.4 Ω DC, marked 4 Ω**, so
     Z = 4 Ω: R = 10 Ω in each leg gives about -15.6 dB, 33 Ω -25 dB, 75 Ω -31.7 dB, 100 Ω -34.2 dB, 150 Ω -37.6 dB,
     220 Ω -40.9 dB, 330 Ω -44.4 dB and 470 Ω -47.5 dB. Use through-hole resistors in series with the two speaker
     wires at J3 (or J4); check there is one in **each** leg. **33 Ω and 75 Ω were still too loud**: an earpiece at the
     ear needs about 1 mW or less, and the amplifier gives up to 3 W, so expect to need about -40 dB or more. Choose R
     so that `vol 4.5` with `tone 1000` (the codec at full scale) is the loudest level ever wanted.
   - **Lower the amplifier's gain.** U4 and U5 are marked **NS4150C**, a filterless class-D amplifier (3 W into 4 Ω)
     whose gain is set by the input resistors (R47 to R50): from memory of its datasheet, `gain = 2 × 150 kΩ / Ri`
     (confirm in the datasheet). Larger Ri lowers the gain. This is an SMD change and it does not attenuate the
     amplifier's own output noise, so the pad above is better.
3. **Rule out the power supply.** Compare the computer's USB port with a phone charger or a battery pack. If the hiss
   changes, the supply is the source (see stage 12).
4. **Other things to try:** `codec w 0x03 0xff` (power down the unused ADC), and `audio off` to confirm the hiss stops
   when the amplifier is off (IO21 low).
5. **Filtering in software cannot remove it.** The hiss is generated after the digital audio.

Record which steps helped in `doc/validation_log.md`.
---

## Stage 7 — Audio input (microphone path)

**Goal:** a clean signal from the line-in jack, with the onboard microphones excluded, only one line-in channel used,
and the best signal-to-noise from the MAX9814.

**DIP:** all OFF.

**How the input path is set up** (by `audio on`, in firmware)
- **Line-in, right channel only.** Measured on this board (2026-09-20): a signal on the plug tip arrives on the codec's
  **right** input (RIN2), not the left the schematic suggests. The codec's left input and left ADC are switched off (ADC
  power register 0x03 = 0xA9) and the right ADC is sent to both I2S slots (register 0x0c = 0x8c). The main firmware
  reads only the left slot, which then carries the right ADC's data.
- **No extra gain after the MAX9814:** the codec's input amplifier is 0 dB (the driver's default is +9 dB) and the ADC
  digital gain is 0 dB.
- **The onboard microphones cannot be excluded in firmware.** They connect to the same codec inputs as the line-in jack
  (LIN2/RIN2, through C18 and C20). **Remove the microphone modules** (test 2).
- **No software filtering** on the microphone path.

**The test commands** (type `audio on 8000` first; `audio on` resets the input settings):

| Command | What it does |
|---|---|
| `mic status` | Shows the input settings. |
| `mic avg [seconds]` | Measures for that many seconds (default 5) and prints the left and right RMS, peak and DC level in dBFS, and whether the right channel is an exact copy of the left. |
| `mic snr [label]` | The signal-to-noise test: 5 s of silence, 5 s of speech, 5 s of silence, with on-screen prompts and a 3-second countdown before each. Prints the silence level before and after, the speech RMS and peak, clipped samples, the signal-to-noise, and a one-line summary tagged with the label. |
| `mic level [seconds]` | Live left and right levels every half second (for watching while you adjust something). |
| `mic chan right\|left\|both` | `right` = the setup above (default). `left` = the mirror image. `both` = both channels, for test 3 only. |
| `mic pga <0-8>`, `mic gain <dB>`, `mic gate on\|off` | Codec input gain (3 dB per step), ADC digital gain, noise gate. Leave them at the defaults. |

Levels are in dBFS: 0 is full scale and lower numbers are quieter (-60 is much quieter than -20).

### Test 1. Noise floor with nothing connected

**Setup:** nothing plugged into the line-in jack; the room quiet; the onboard microphones still fitted.
**Type:** `audio on 8000`, `mic status`, `mic avg 10`.
**Expect:** both channels at a low level. **Record** the left and right RMS and the DC.

### Test 2. Onboard microphones: show the problem, then remove them

**2a. With the microphones fitted.** **Setup:** as in test 1. **Type:** `mic snr mics-fitted` and follow the prompts,
speaking close to the board during the speech phase.
**Expect:** `speech : rms ...` lines and a signal-to-noise figure: the microphones are heard through the line-in path
(the documented board bug). **Record** the speech RMS.

**2b. Remove the microphone modules** (note their markings and designators) and repeat: `mic snr mics-removed`.
**Expect:** **`NO VOICE DETECTED`**. That is the pass. If a voice is still detected, another microphone part is fitted:
look for it on the board, or as a fallback remove C18 and C20.

### Test 3. Only one line-in channel is used (the other cannot mix in)

**Setup:** a phone or player with a **stereo test tone** (search for "stereo channel test", or record a tone panned fully
left or fully right). Plug it into the line-in jack. Turn it up to a moderate level.

| Step | Play | Type | Expect |
|---|---|---|---|
| 3a | tone on the **left** channel only | `mic chan both`, then `mic avg 5` | left RMS loud (well above the noise floor of test 1); right near the floor |
| 3b | same tone, left only | `mic chan right`, then `mic avg 5` | **both left and right near the floor**; "right equals left in 100%" |
| 3c | tone on the **right** channel only | `mic avg 5` (still `chan right`) | left and right both loud and equal; "right equals left in 100%" |

**Pass:** 3b shows no sign of the left-channel tone. If it fails, the register values need correcting: read the
registers with `codec r 0x03` (expect 0xA9), `codec r 0x0c` (expect 0x8c), correct them from the ES8388 datasheet with
`codec w`, and record the working values.

**Done 2026-09-20 without a stereo source.** There was no cable for a tone, so the register values were checked instead
(the earlier left-only setup read 0x59 and 0x4c as expected), and the MAX9814 on the plug tip showed up on the right
channel only with `mic chan both` (left −80 dBFS, right moving with claps). That showed the tip is the right input. The
setup is now right-only (0xA9 and 0x8c); after reflashing, `codec r 0x03` and `codec r 0x0c` should read those values, and
`mic avg` with the module quiet should read "right equals left in 100%".

### Test 4. MAX9814 gain: 40, 50 and 60 dB

**Setup:** the MAX9814 module built as in `initial_design.md` "Microphone" (supply filter, R2, jumper headers), wired to
the line-in tip and sleeve. The GAIN strap picks the gain: **to VDD = 40 dB, to GND = 50 dB, removed = 60 dB**.
Keep the module at the distance it will have from the mouth.
For each setting, **power the module off and on** after moving the strap, then:
**Type:** `audio on 8000`, then `mic snr 40dB` (or `50dB`, `60dB`). Follow the prompts; in the speech phase count
"one two three ..." at a normal level.

**Record** in this table (copy the values from the printed lines):

| GAIN pin | Silence before | Silence after | Speech RMS | Speech peak | Clipped samples | Signal-to-noise |
|---|---|---|---|---|---|---|
| 40 dB (VCC) | −58.9 | −21.4 (not silent; invalid) | −21.0 | −5.8 | 0 | 37.9 (against the first silence) |
| 50 dB (GND) | not run | | | | | |
| 60 dB (open) | not run | | | | | |

**Result so far (2026-09-20).** Module at about 5 cm, right channel, sleeve-only ground. The first silence was room
noise (−58.9 dBFS, against a quiet-room floor of −77 to −82) and the second silence was not silent, so only the first
figure counts. **40 dB is the lowest gain the module offers and speech already peaks at about the −6 dBFS limit, so 50 and
60 dB would clip and were not run. Decided: 40 dB, A/R open, no further tuning.** If the microphone is too hot in the
handset, attenuate it physically (foam or cloth over the capsule opening). Speech and clap peaks are strong: claps reach
0 dBFS.

**How to read it**
- **Signal-to-noise** is speech RMS minus silence RMS. **Aim for at least 40 dB.**
- **Speech peak** must be **at or below -6 dBFS**, with **no clipped samples**.
- **Silence after** much higher than **silence before** means the AGC is boosting room noise in the pauses.
- **Choose** the setting with the best signal-to-noise that meets the peak and clipping limits. If two are close,
  take the lower gain.
- Then try the attack/release pin (A/R: open = 1:4000, **linked to VDD = 1:2000, to GND = 1:500**) at the chosen gain, for example
  `mic snr 40dB-AR2000`, and keep the setting with the smaller rise after speech.

### Test 5. Power source (ground-loop hum)

With the chosen gain: `mic snr usb` powered from the computer's USB port, then `mic snr charger` powered from a phone
charger or battery pack (and no computer connection except for the console, if you must). **Record** both
signal-to-noise figures. A better figure on the charger means the computer's USB adds noise.

### Test 6. The real cord

Connect the MAX9814 through the **8-conductor cord** with the planned wire use, then: `mic avg 30` (silent, 30
seconds), and `mic snr cord`. **Expect** the silence level and signal-to-noise within about 3 dB of the direct
wiring. A worse result means pickup or a shared-ground problem in the cord.

### Test 7 (later, with stage 11). Ring driver running

Repeat `mic avg 30` and `mic snr ringing` while the bell is being driven, to check for interference on the input.

### Wiring and noise notes (hardware only)

- **The build is specified in `initial_design.md`, section "Microphone"** (parts, jumper headers, cord wire table,
  wiring diagram, checks). Tests 1 to 3 need no MAX9814. Build the module (`r1`, `r2`, `c1`, `c2`, GAIN strap to
  `max_vdd` = 40 dB, A/R open) before test 4, run its unpowered and powered checks, and for tests 4 and 5 connect it to
  the 3V3 header pin, a GND header pin and the line-in plug (tip, sleeve) with short wires. Test 6 then uses the 8-wire
  cord.
- **Clean supply at the module:** the 3.3 V header comes from a switching regulator. `r1` (22 Ω) with `c1` (47 to
  100 µF) gives a corner of about 70 to 150 Hz, and `c2` (100 nF) sits across the pins.
- **Keep supply current out of the signal return.** The cord's 8 straight wires are used as: 1 supply, 1 mic signal, 2 in
  parallel for the power ground, 2 in parallel for the signal ground (the jack sleeve) and 2 for the earpiece. Join the
  two grounds only at the module's GND pin. Twisting is not needed: the mic OUT is a low-impedance output (with `r2`,
  220 Ω, in series) and the earpiece pair carries only millivolts (the 820 Ω pad is at the amplifier end).
- **Why the gain is measured:** more gain does not improve the MAX9814's own signal-to-noise (its input noise is amplified
  with the speech); it only helps against noise added afterwards. The handset mic is close to the mouth, and (from
  memory of the datasheet) the AGC can lower the gain only about 20 dB, so 60 dB can clip a loud close voice and boosts
  room noise in the pauses.
- **Optional hardware high-pass, only if hum or rumble is a problem:** a film capacitor in series between OUT and the
  jack tip; the corner is `1 / (2π · R_in · C)` with R_in the codec's line-in resistance (10 kΩ with 0.1 µF is 160 Hz).
- **Placement:** keep the module away from the earpiece (feedback).

**Pass:** the microphones report `NO VOICE DETECTED`, the unused (left) channel cannot mix in (test 3b), and the chosen MAX9814
setting gives a signal-to-noise of at least 40 dB with speech peaks at or below -6 dBFS and no clipping, also through the
real cord.

**Status (2026-09-20), not yet passed:** done: tests 1, 2 (mics removed, no voice detected, floor −82 dBFS), 3 (by register
readback) and 4 (40 dB chosen; signal-to-noise 37.9 dB against the first silence, peak −5.8 dBFS, so just short of 40 dB and
at the peak limit). Not done: a clean re-run of the 40 dB test with a truly silent second phase, test 5 (USB against a
charger), test 6 (the real cord, not yet built) and test 7 (with the ring driver). Tuning is stopped for now.
---

## Stage 8 — SD card for voice prompts

**Goal:** store many higher-resolution recordings on the SD card and play them reliably.

**DIP:** SW3 ON (IO15 to SD CMD), rest OFF (SW2 OFF keeps DATA3 pulled up).

**Rules for this stage:**
- **Never let the code format the card.** The existing SD code in this repo
  (`components/utility/sample.c`) sets `format_if_mount_failed = true`; do not reuse that setting.
- Leave the SD card **out** while flashing until step 3 proves it is safe.

**Tool:** the `hwtest` command `sd` (see `hwtest/README.md`). It mounts in 1-bit mode, never formats, and mounts
**read-only by policy** unless you type `sd mount rw`. Which command does which step:

| Step | Commands |
|---|---|
| 1 and 2 (mount, list, write and read back) | `sd mount`, `sd ls`, then `sd unmount`, `sd mount rw`, `sd test` |
| 3 (boots and flashes with the card in) | reset with the card inserted, then `sd mount`, `sd check`; and flash once with it inserted |
| 4 (speed and latency) | `sd speed 4` |
| 5 (playback) | `sd gen t16.wav 16000 20`, `audio on 16000`, `audio out spk` (keep `vol` low), `sd play t16.wav`; repeat with `sd gen t8.wav 8000 20`, `audio on 8000`, `sd play t8.wav`. **Done at 16 kHz 2026-09-20:** 0 of 157 chunks over budget, sounded clean. |
| 6 (hundreds of files, and the bank) | `sd scale make 500`, `sd scale open`; `sd bank make 16`, `sd bank seek`; repeat `sd bank seek` after rebuilding with `CONFIG_FATFS_USE_FASTSEEK=y` |
| 7 (reliability) | after each power cycle `sd mount`, `sd check`; and `sd cd` while inserting and removing the card |
| 8 (card sizes and brands) | repeat steps 1 to 4 with a second card |

`sd mount` and `sd check` should print PASS after each of the 50 power cycles. The build needs `CONFIG_FATFS_LFN_HEAP`
(added to `sdkconfig.defaults`), which only takes effect on a fresh configuration: delete `hwtest/sdkconfig` and rebuild.

1. **Set the DIP switches** for SD: **SW2 OFF** (leave DATA3 pulled up on the card side), **SW3 ON**
   (IO15 to CMD). Use **1-bit SDMMC mode** (needs only IO14, IO15, IO2), and mount a FAT32 card
   read-only first. The card's DATA lines are pulled up on the board.
2. **Mount and list files.** Then read and write a test file, and read it back and compare.
3. **Boot and flash with the card inserted.** Some boards refuse to boot with a card in the slot. **Fail**
   means we must document "remove the card to flash," or add a workaround. Check both **power-on** and
   **flashing** with the card present. (GPIO2, 12 and 15 have pull-ups on the SD side and are strapping
   pins.)
4. **Speed test:** measure sequential read speed in 1-bit mode. Voice at 16 kHz, 16-bit mono needs about
   32 KB/s (8 kHz needs 16), so the requirement is easily met; the goal is finding **latency** and **hiccups**.
5. **Playback:** stream a 16-bit mono WAV from the SD card (a clip inside the single bank file, or a separate file) to the codec at **8 kHz and 16 kHz** (the
   two banks in the design; nothing higher is used) while the rest of the system idles. Listen for dropouts. Try streaming while Bluetooth is
   active (stage 9 repeats this).
6. **Scale test:** put **hundreds of files** on the card (the plan has many prompts and variants) and
   measure the time to open a file by name. Check that file names follow the clip IDs in
   `audio_clips.md`. **Then compare the bank option** (`audio_clips.md`: one large WAV plus an index): seek to random
   clips inside one file of the same total size and measure the seek plus first read, with and without
   `CONFIG_FATFS_USE_FASTSEEK`. Use the bank if that is under about 20 ms.
7. **Reliability:** cycle power 50 times with the card inserted; remove and reinsert it while running
   (card-detect is on IO34). Confirm no corruption.
8. **Card sizes and brands:** try at least two cards. The spec claims support for cards up to 64 GB.

**Decisions this stage feeds:** the recording sample rate and format for `audio_clips.md`; 1-bit vs 4-bit
mode (4-bit needs IO4, IO12 and IO13, which the GPIO budget cannot spare); whether to keep a tiny set of
built-in fallback prompts for when the card is missing.

**Pass:** reliable mount, playback at the chosen rate without dropouts, boots with the card in, hundreds of
files fine.

**Results so far (2026-09-20).** 1-bit SDMMC mode at 20 MHz works. Two cards tried; long file names needed
`CONFIG_FATFS_LFN_HEAP` (the I2S driver also needed `intr_alloc_flags = 0` once the card was mounted, see
`initial_design.md`).

| | 128 MB SDSC, FAT16, 2 KB clusters | 64 GB SDXC ("SD64G"), FAT32, 32 KB clusters |
|---|---|---|
| Mount | 300 ms first, about 40 ms later | 293 ms first, 52 ms later |
| Write/read-back test | PASS | PASS |
| Write speed | 1501 KB/s | 2098 KB/s |
| Sequential read, 4 KB chunks | 545 KB/s; 7.3 ms per read, max 8.1; none over 20 ms | 599 KB/s; 6.7 ms per read, max 7.3; none over 20 ms |
| Bank: random seek + 4 KB read (16 MB file, fast seek off) | median 15.1, 95th percentile 28.9, max 33.9 ms | median 8.0, 95th percentile 9.4, max 10.1 ms |
| 500 files: open by name + read 512 bytes (stdio) | avg 28.4, 95th percentile 51.3 ms; listing 96 ms | avg 22.0, median 21.4, 95th percentile 41.2, max 41.8 ms; listing 84 ms; all found, 0 wrong |
| 16 kHz and 8 kHz playback | 0 of 157 and 0 of 40 chunks over budget; sounds clean | not run |

- **Cards over 32 GB come as exFAT and do not mount**; they must be formatted FAT32 with an MBR map on the Mac
  (`diskutil eraseDisk FAT32 NAME MBRFormat /dev/diskN`). The first card had a FAT32 layout with too few clusters
  (125 MB), which FatFs also rejects ("no filesystem"); `diskutil eraseDisk MS-DOS` made it FAT16 and it mounted.
- **Seek time depends on the cluster size**, not on the card: with 2 KB clusters a 16 MB file is 8192 clusters to walk;
  with 32 KB clusters it is 512. The bank meets the 20 ms target on the large-cluster card without fast seek.
- **Read speed was limited by stdio, not the card or bus.** On the 64 GB card, `fread` gave about 570 KB/s at any chunk
  size (stdio refills a small internal buffer), while POSIX `open()`/`read()` gave **1904 KB/s at 4 KB chunks (2.1 ms
  each) and 2354 KB/s at 64 KB**. The bank's random seek plus 4 KB read dropped from 8.3 ms (95th percentile 9.1) with stdio to
  **3.2 ms (95th percentile 4.7)** with POSIX. Writes, which use large `fwrite`s, were already 2.1 MB/s. The main firmware
  should use POSIX calls (or raise `CONFIG_FATFS_VFS_FSTAT_BLKSIZE`). Even the stdio speed is far above the 32 KB/s a
  16 kHz stream needs.
- **The 128 MB card shows the same pattern** (POSIX 4 KB reads 2.6 ms and 1532 KB/s, against stdio 7.6 ms and 526 KB/s),
  but its bank seek stays above the target even with POSIX reads: median 11.6 ms, 95th percentile 24.9 ms (stdio 15.4 and
  29.9). With 2 KB clusters the seek itself costs about 9 ms, against about 1 ms on the 64 GB card. Use a card formatted with
  large clusters for the bank.
- **Flashed with the card inserted: worked (2026-09-20).**
- **Swapping the card while mounted:** the mount does not notice; the next access fails with a timeout
  (`sdmmc_host_wait_for_event returned 0x107`, then "No such device"), and `sd unmount` then `sd mount` brings the new card
  up. The card-detect pin IO34 read 0 in both readings taken (unclear whether the card was in and out), so it may not
  toggle; run `sd cd` with the card in, out, in again to settle it. Not needed: hot swap is a nice-to-have, and the firmware
  can do it by mounting read-only, treating a timeout as "card gone" (unmount) and retrying the mount on demand or every few seconds.
- **Per-clip files against the bank:** opening a file by name in a flat 500-file directory took about 22 ms on the large
  card (95th percentile 41 ms), against 3.2 ms for a bank seek with POSIX reads, and it grows with the number of files
  (linear directory scan). This supports the bank design.
- **Skipped by decision (2026-09-20): the 50 power cycles.** Evidence instead: several boots and remounts on both cards, a
  flash with the card inserted, and a card swap, all clean. Accepted risk: a rare card that fails to mount after a power
  cycle would be caught by the main firmware retrying the mount (see the hot-swap note above).
- **Cleanup:** `sd scale clean` removes the 500 test files.

---

## Stage 9 — Bluetooth handsfree

**Goal:** pair with a real phone, place and receive a call, and pass voice audio both ways.

**DIP:** SW3 ON, rest OFF (SD needed for the step 7 coexistence test).

**Tool:** the `hwtest` command `bt` (see `hwtest/README.md`; source `main/cmds_bt.c`). It mirrors `bt_task.c`'s Bluetooth
setup, but pairing and calls are console commands, because the dial is not ready. Before the first `bt on`: rebuild from a
fresh configuration (`rm sdkconfig`, then `idf.py build`); the Bluetooth options are in `sdkconfig.defaults`. Set the
earpiece first: `audio out spk` and a low `vol` (the call audio uses the codec, restarted at 8 or 16 kHz per call).

| Step | Commands |
|---|---|
| 1 (bring up, discoverable) | `bt on`, `bt status`, `bt pair` |
| 2 (pairing) | `bt pair` (Just Works is the default and works with an iPhone), pick `weeBell-test` on the phone, choose a device type if asked. Passkey entry (`bt iocap in`, then `bt passkey NNNNNN`) failed on the iPhone; `bt iocap io` gives numeric comparison (`bt yes` / `bt no`) |
| 3 (connect, calls) | `bt connect` (or the phone connects itself), `bt dial 1NNNNNNNNNN`, `bt answer`, `bt hangup`, `bt dtmf 5`, `bt calls` |
| 4 (voice audio) | during a call the log shows `call audio: connected (CVSD, 8 kHz)` or `(mSBC, 16 kHz)`; talk and listen; `bt stats` |
| 5 (latency, echo) | listen for delay and echo; `bt nrec` asks the phone to turn off its own echo cancellation (what the main firmware does today) so you can compare with and without |
| 6 (reconnect) | reset the board and the phone; `bt status` after 10 to 20 s (auto-connect retries every 10 s; `bt auto off` to stop it) |
| 7 (coexistence) | `sd mount`, `sd play` and `audio` commands during a call; watch `bt stats` for underruns |

**iPhone results so far (2026-09-20).** Passkey entry (`bt iocap in`, KeyboardOnly) **failed**: the iPhone connected, our
stack requested a passkey, the iPhone never displayed one, and pairing timed out after about 30 s (authentication failure
reason 5, "connection unsuccessful"). **Just Works (`bt iocap none`) paired at once** and HFP connected (service-level
connection, with the phone's battery and signal indicators arriving). After pairing the iPhone shows its own "device type"
menu (headphones, speaker, car stereo and so on): that is iOS's 14.4+ classification of third-party audio devices, used
for headphone-volume notifications, and I found no accessory-side field that skips it. Apple's guidelines (section 2.1.5)
name only Numerical Comparison, so passkey entry through the dial is not a path an iPhone supports; see the design doc.

**Audit against Apple's *Bluetooth Accessory Design Guidelines for Apple Products* (R7, the copy I could read; the current
*Accessory Design Guidelines* PDF was too large to fetch, so newer requirements, including any class-of-device section, are
unchecked).**

| Apple requirement (section) | What `hwtest` sends | Status |
|---|---|---|
| Core spec 2.1 + EDR or higher (2.1) | Bluetooth 4.2 BR/EDR | OK |
| Support and request sniff mode; accept role switch (2.1.2, 2.1.3) | the phone requested sniff and the link entered it | OK (role switch not observed) |
| Extended Inquiry Response has the local name and TX power (2.1.4) | name and UUIDs by default; TX power was off | **Fixed**: `bt on` now sets TX power and UUIDs |
| Secure Simple Pairing; Numerical Comparison if there is a display and input (2.1.5) | SSP on; no display, so Just Works | OK (passkey entry is not offered) |
| Device ID profile 1.3+ with a Bluetooth SIG vendor ID (2.2.1) | none was sent | **Added** with the stack's own function (IDF 4.4.4 has no public API): USB-IF source, pid.codes test vendor and product `0x1209/0x0001`, version `0x0100`. A hobby device has no SIG company ID; change the `DID_*` constants if it gets IDs. |
| HFP 1.5 or higher (2.2.2) | 1.6 with wide-band speech | OK |
| Support remote volume control and voice-recognition bits in BRSF (2.2.2.1, 2.2.2.3) | the stack sends echo/noise, 3-way, caller ID, voice recognition, remote volume, enhanced call status and control, and codec negotiation | OK |
| Use indicator events, not polling (2.2.2.2) | the stack enables them (indicator events arrive) | OK |
| Turn off the phone's echo cancellation with `AT+NREC` only if the accessory does its own (2.2.2.4) | not sent | OK: the design has no local echo canceller. `bt nrec` is available for a comparison. |
| In-band ringing (2.2.2.5) | the bell rings instead | not used by design |
| eSCO S2/S3 and audio rendered within 40 ms of the link opening (2.2.2.6) | S-settings are the stack's; call audio starts after a codec restart | **Measure**: `bt on` and a call now print how many ms the bridge took to start |
| Wide-band speech with T2 (2.2.2.7) | mSBC enabled | OK |
| `AT+XAPL` after the service-level connection, `AT+IPHONEACCEV` for battery (4, 5) | not sent | **Not possible in IDF 4.4.4** (no API); only needed for battery display and Siri status |

**Call audio results (2026-09-20, iPhone, dial-out, `hwtest` `bt`).** mSBC at 16 kHz was negotiated; the codec restart took
182 ms (Apple asks for audio within 40 ms). After enlarging the jitter buffers (30 ms earpiece side, 20 ms microphone side,
64-frame blocks, 24 ms of I2S buffering) a 54 s call had **0 gaps, 0 trims and 0 drops** in both directions (7252 packets of
240 bytes each way); before that the far end reported crackle. Levels: from the phone **peak 0.0 dBFS, average −15.5 dBFS** (a
hot signal, so the earpiece is quiet because of the 820 Ω pads, sized with a full-scale tone, not because the signal is low:
changing 820 Ω to 330 Ω per leg would give about +8 dB; the phone's signal uses the full 16-bit range: peaks at 0.0 dBFS in both
calls, average −15.5 and −17.0 dBFS); to the phone peak −1.6 dBFS. The far end reported **clipping when the microphone is 2
cm from the mouth and good audio at 30 cm**. **`bt gain tx -12` (digital, after the codec) fixed it**, with a second call
peaking at −5.6 dBFS: so the distortion was not the MAX9814 saturating (an earlier guess); the uplink was simply too hot at
close range. **The microphone default is now −12 dB** (the design's microphone volume digit 6 is about −12.8 dB); no foam
is needed. To make the earpiece louder: try `vol 4.5` first (+4.4 dB over `volstep 0`), then smaller pad resistors (330 Ω:
+8 dB, 220 Ω: +11 dB, 150 Ω: +15 dB, more hiss). Still to do: echo (`bt nrec`), reconnect after a reset, coexistence with the SD card.

**Earpiece pads changed (2026-09-20): 160 Ω in each leg, replacing 820 Ω**, after trying the earpiece profiles (about −38 dB;
roughly 14 dB louder). Earlier stage 6 results (the volume steps, "slightly loud", no audible hiss) were with 820 Ω, so
re-check the hiss with the call idle and the volume-step spacing with the new pads.

**Equalizer test procedure (tools built 2026-09-20, not yet run).** `eqp list` shows ten microphone and ten earpiece
profiles (design: "Equalizer profiles"). To measure the *earpiece and housing response*: put the earpiece and a reference
microphone (a phone or an audio interface) in the final housing position, `audio on 16000`, `audio out spk`, a low `vol`,
`eqp spk 0`, `sweep` (or `noise`) and record it on the other device; repeat with `eqp spk <n>` to see what each profile
does. To measure the *microphone in its housing*: put a reference speaker at the mouthpiece, play a sweep or noise from it
(a phone), `eqp mic 0`, `rec start raw.wav 60`, then repeat with `eqp mic <n>`; compare the recordings' spectra. The sweep tone
k is at `40 Hz + 10 Hz x k` and starts `k x 300 ms` in, so a script can cut the recording into segments and measure each
tone's level. During a call, `eqp mic <n>` and `eqp spk <n>` change the sound live.

**CPU cost of software tone shaping (`bt bench`, measured 2026-09-20; now `eqp bench`).** With the product's settings (`-O2`, 240 MHz; `hwtest`
now uses both) a high-pass plus a high shelf costs **96 cycles per sample = 0.64% of one core** at 16 kHz (high-pass alone
74 cycles, 0.49%); both directions with both filters about 1.28%. With the default debug build at 160 MHz it was 173 cycles,
1.73%. The iPhone's own noise cancellation was never turned off in `hwtest` (`bt nrec` is manual only); the main firmware's
`esp_hf_client_send_nrec()` at `bt_task.c:983` should be deleted, as the design already says.

The class of device is not covered by the R7 text. `bt cod <minor> [major [service hex]]` sets it; `bt cod 1 4 120` gives
0x240404, the value common headsets use. **Result: the iOS device-type menu (Car Stereo, Headphone, Hearing Aid, Speaker,
Other) appears with hands-free (minor 2) and with loudspeaker (minor 6); no class stops it.** It is one tap at pairing, and
"Speaker" was chosen. The default stays hands-free, the honest class for this device. Note that a device the phone treats as
a speaker may be offered for music, but this device has no A2DP: it carries phone-call audio only.

Found while preparing: the main firmware's `sdkconfig` has **`CONFIG_BT_SSP_ENABLED` off**, so its `#if (CONFIG_BT_SSP_ENABLED
== true)` blocks (including the passkey and numeric-comparison handling) are compiled out and it pairs with a legacy PIN. The
passkey-entry design needs it **on**; `hwtest` turns it on. (With SSP on, a phone that supports SSP will not use the legacy PIN.)
The Bluetooth image (about 1.1 MB) also does not fit the default 1 MB app partition, so `hwtest` uses the large single-app
partition table (1.5 MB); check the main firmware's partition table the same way when it gets SSP and the SD code.

1. **Bring up Classic Bluetooth** with the HFP client (as `bt_task.c` does) and confirm the device is
   discoverable and connectable. Use the ELEGOO board first if it is quicker; the code path is the same.
2. **Pairing:** test both the current numeric-comparison method and the planned **passkey entry**
   (`ESP_BT_IO_CAP_IN`), typing the passkey through the test console (standing in for the dial). Make sure
   the passkey event is actually delivered (it is currently compiled out in `bt_task.c`).
3. **Service connection and calls:** connect HFP, place a call from the console, receive a call, answer,
   hang up.
4. **Voice audio:** confirm the SCO audio arrives at 8 kHz, and at 16 kHz (wide-band, mSBC) if the phone
   uses it. Play it to the earpiece and send the microphone back.
5. **Latency and quality:** listen for delay and echo. Decide the echo-canceller question from
   `initial_design.md` (drop the line echo canceller and stop asking the phone to disable its own).
6. **Reconnect behavior:** power-cycle the board and the phone; check the auto-reconnect.
7. **Coexistence:** run stage 6 and stage 8 audio during a call. Watch for CPU load, underruns and the
   Bluetooth controller starving.

**Pass:** stable pairing, call control, and clean two-way voice.

---

## Stage 10 — Dial and hook inputs on the real phone

**Goal:** reliable digit and hook detection from the actual contacts.

**DIP:** SW3 + SW4 ON, rest OFF (final config; hook on IO13 / MTCK if stage 4 confirms).

1. **Wire the contacts** to the chosen GPIOs (stage 4): pull-ups on the dial inputs, and for the hook on IO13 / MTCK the contact between 3V3 and MTCK (active-high, R15 is the pull-down), the 10–100 Ω series resistors and, if
   the wiring is long, small filter capacitors.
2. **Log raw edges** with microsecond timestamps while dialing each digit 1–9 and 0. Measure the pulse rate
   (expect about 10 per second) and break/make ratio (about 60/40), and note how much your dial deviates.
3. **Decode digits** and verify all ten, including 0 (ten pulses). Try slow and fast dialing.
4. **Hook switch:** measure bounce; pick up and hang up repeatedly and slowly; confirm a hang-up is
   distinguished from a dial pulse (with separate contacts this is trivial).
5. **Noise:** run these tests with the ring driver connected and powered (stage 11) to check for
   pickup on the contact lines.

**Pass:** all digits decode correctly over 100 trials, hook detection is clean.

---

## Stage 11 — Ring driver

**Goal:** ring the bell safely and find its real resonance and voltage needs.

**DIP:** SW3 + SW4 ON, rest OFF.

**Safety:** this stage involves a boost converter output of up to 45 V DC. Use a current-limited bench
supply if you have one, insulate connections, and never touch the output while it is running.

1. **Bench first, no bell:** connect the DRV8825, the boost stages, and a **resistive dummy load** (about
   4 kΩ) in place of the bell. Set the boost stage 2 output low (about 12 V). Confirm the DRV8825
   current-limit trimpot and its DIR, nSLEEP and nRESET wiring.
2. **Boot safety:** with the external pull-up on nENABLE, confirm the output stays off during power-up
   and reset (stage 4 step 3).
3. **STEP generation:** produce STEP with a hardware timer (LEDC or MCPWM) and view the output on a scope
   or meter across the dummy load. Confirm the **STEP-to-output frequency ratio** (about 4 steps per output
   cycle in full-step mode is the expectation).
4. **Connect the bell** at low voltage. Sweep the drive frequency from about 15 to 35 Hz to find the
   loudest (resonant) point, recording it.
5. **Raise the voltage in steps** and record loudness against voltage; stop at the level that is loud
   enough. Check the DRV8825 and boost modules for heat.
6. **Cadence:** run the 2 s on / 4 s off cadence. Also test the UK double-ring cadence from `tones.md`.
7. **Fault behavior:** stop the firmware mid-ring (for example by resetting it) and confirm the bell goes
   quiet and nothing overheats.
8. **Interference:** while the bell is ringing, check the audio path (stage 6 and 7) for hum and clicks,
   and the dial contact lines (stage 10) for false pulses.

**Pass:** the bell rings at a bench-tuned frequency and voltage, silent at boot, no interference.

---

## Stage 12 — Power

**Goal:** understand power behavior on USB and on battery.

**DIP:** SW3 + SW4 ON, rest OFF.

1. **Battery connector:** identify the connector type and the **polarity** with the meter before plugging in
   a LiPo. Use a protected LiPo.
2. **Current draw** in idle, Bluetooth connected, audio playing, in a call, and ringing. Record each.
3. **Battery operation:** run from the battery alone. The schematic shows no boost converter, so the "5 V"
   rail is just the battery voltage and the 3.3 V buck can drop out near 3.5 V. Discharge slowly and record
   the voltage at which the board browns out.
4. **Brown-out behavior:** confirm the reset and log that occur, and decide whether to enable the
   brown-out detector.
5. **Charging:** connect USB while on battery; confirm charging works and the LEDs behave.
6. **Ring boost supply:** confirm the ring boost chain does not pull the board's rail down when the bell
   starts.

**Pass:** a documented current table, a known brown-out voltage, and charging that works.

---

## Stage 13 — Integration soak

**Goal:** everything together, for hours.

**DIP:** SW3 + SW4 ON, rest OFF.

1. **Run the combined scenario** on the real hardware: Bluetooth call with voice, SD prompt playback,
   dialing, and ringing on incoming calls, repeated in a loop.
2. **Soak 24 hours** on USB power and again on battery, logging resets, reset reasons, heap, stack high-water
   marks, and CPU use per core.
3. **Look for:** crashes, watchdog resets, audio underruns, SD errors, Bluetooth disconnects, and heat.

**Pass:** 24 hours without an unexplained reset or audible fault.

---

## Definition of done

Validation is finished when every row below is **confirmed working** on the real board.

| Capability | Stage | Confirmed |
|---|---|---|
| USB serial and flashing (method documented) | 1 | 2026-09-19 |
| Our own code runs; console output visible | 2 | 2026-09-19 |
| Module variant and I2C pins identified | 3 | 2026-09-19 |
| PSRAM (8 MB chip, about 4 MB in the heap) | 3 | 2026-09-19 |
| GPIO plan proven, works with SD (boot-state check optional) | 4 | 2026-09-19; boot check skipped, accepted risk, checked in stage 11 |
| Codec control over I2C | 5 | 2026-09-19 |
| Earpiece audio out, chosen sample rates, even volume steps | 6 | 2026-09-20 |
| Microphone in (MAX9814), onboard mics isolated, gain chosen | 7 | |
| SD card: mount, boot-safe, playback at 8 and 16 kHz, hundreds of files | 8 | 2026-09-20; the 50-power-cycle test skipped, accepted risk |
| Bluetooth: pairing (Just Works: passkey entry does not work with an iPhone), calls, two-way voice | 9 | in progress: pairing, dial-out call and two-way 16 kHz voice work (2026-09-20); reconnect, incoming call, SD coexistence open |
| Dial and hook decoding on the real contacts | 10 | |
| Ring driver: frequency, voltage, cadence, boot-safe | 11 | |
| Power: current table, battery operation, brown-out, charging | 12 | |
| 24-hour combined soak | 13 | |

## Decisions that hang on the results

| Decision | Depends on |
|---|---|
| Final pin assignments | stages 3 and 4 (module variant, GPIO budget with SD) |
| Whether to keep the dial-in-progress input | stage 4 |
| Codec I2C/I2S pins in the firmware | stage 3 |
| Clip format (decided: 8 kHz and 16 kHz banks); SD 1-bit vs 4-bit; fallback prompts | stage 8 |
| Volume ranges (decided: keep the `gain.h` values) | stage 6 |
| Echo canceller kept or dropped | stage 9 |
| Ring frequency, voltage, cadence | stage 11 |
| Battery cutoff and whether to read battery voltage | stage 12 |

## Results log template

Copy this into a log file (for example `doc/validation_log.md`) and fill it in.

| Date | Stage | Step | What I did | Result (pass/fail) | Notes, values, photos |
|---|---|---|---|---|---|
| | | | | | |
