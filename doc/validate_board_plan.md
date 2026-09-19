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

1. **Log the chip and flash info** from the boot log and the firmware (`esp_chip_info`, flash size).
2. **Enable PSRAM** (64 Mbit, auto-detect) and print `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`.
   Expect about **8 MB**. **Fail** if PSRAM init errors: check the mode setting and note the message.
3. **Scan I2C on both candidate pin pairs** (leave everything else idle). Do **not** drive GPIO0.
   - Pair A: **SDA 18 / SCL 23**
   - Pair B: **SDA 33 / SCL 32**
   The ES8388 answers at **0x10**. Record which pair answered. **This decides the whole pin plan.** The
   newest module answers on pair A; older ones on pair B.
   *If neither answers:* check that GPIO0 is left alone, the module 3.3 V, and try the AC101's address
   **0x1A**.
4. **Record** the results in the log, and update `audio_kit_v2.2.md` (sections 4 and 9).

**Pass:** PSRAM is about 8 MB, and one I2C pair shows a device at 0x10 (or 0x1A).
**Decision it feeds:** newest vs. older module; whether GPIO 5, 18 and 23 are free.

---

## Stage 4 — GPIOs and switches

**Goal:** find out which GPIOs we can actually use, given the board's wiring and the SD card requirement.

**The SD card changes the GPIO plan** (recorded in `initial_design.md`). In 1-bit mode the SD slot needs
**IO14 (CLK), IO15 (CMD) and IO2 (DATA0)**; 4-bit mode also needs IO4, IO12 and IO13. The proposal in
`audio_kit_v2.2.md` section 6 used IO13, IO14 and IO15 as inputs, which now **conflicts** with the SD card.

1. **Multimeter first** (power off unless noted):
   - Measure the **resistance from GND to each header GPIO** (IO19, 23, 18, 5, 22, 21) and the idle voltage
     when powered. Note anything unexpected.
   - Confirm the DIP switches: with a beeper or ohmmeter, verify what each switch connects (from the spec:
     SW1 IO13–KEY2, SW2 IO13–DATA3, SW3 IO15–CMD, SW4 IO13–MTCK, SW5 IO15–MTDO).
   - Find **R15** (a resistor to ground on the JTAG lines), **R46** (between IO21 and the amp enable),
     **R67–R70** (key GPIO series resistors) and note their values, which the schematic does not print.
2. **Firmware GPIO test** for each candidate pin: drive it as an output and watch an LED or the meter;
   read it as an input with an external pull-up while touching the pin to ground.
3. **Boot-state check (bell safety):** with a meter or scope on each pin we plan to use as an **output**
   (STEP and nENABLE), watch the level during power-up and during a reset. Nothing should glitch high on
   the bell driver's enable, and nothing should toggle before firmware starts.
4. **Check the button ladder coupling:** with pull-ups fitted, ground one key GPIO and watch the others.
   Repeat after removing the relevant series resistor (R67 for IO19).
5. **Work out the GPIO budget with the SD card.** We need **3 inputs** (hook, dial pulse, dial-in-progress)
   and **2 outputs** (STEP, nENABLE), and the SD slot takes IO14, IO15, IO2. Candidates that remain:

   | Candidate | How it becomes usable | Note |
   |---|---|---|
   | **IO13** | P1 pin 2 with DIP SW4 ON; SW1 and SW2 OFF | keep SW2 OFF so DATA3 stays pulled up on the card side |
   | **IO22** | free (LED4) | output |
   | **IO19** | remove R67 to detach the key ladder | output (LED5 follows it) |
   | **IO21** | remove R46 so the amp-enable line is isolated | the amps then stay off (R51 pulls their enable low) |
   | **IO12** | P1 pin 3 | strapping pin; only after reading the eFuse result in stage 1 |
   | IO18, 23, 5 | free only on an **older** module (on the newest they are the codec's) and after removing R68–R70 | depends on stage 3 |

   That gives **four** clean GPIOs on any module and a fifth only via IO12 or an older module. If five are
   not available, the options are: (a) drop the dial-in-progress input and rely on hook plus pulse only
   (the current firmware's digit timing already works this way); (b) add a small I2C GPIO expander on the
   codec's I2C bus (only possible on the newest module, where that bus is on the exposed IO18/IO23 pins);
   (c) use IO12 as an input after checking the strapping behavior. **Decide here** and update the design.

**Pass:** a documented, tested list of GPIOs for hook, pulse, (dial-in-progress), STEP, nENABLE that
works with the SD card, and the boot-state check is clean.

---

## Stage 5 — Codec control

**Goal:** talk to the ES8388 over I2C, with no audio yet.

1. **Initialize I2C** on the pair found in stage 3 at 100 kHz, address 0x10.
2. **Read and write registers** using this repo's `es8388.c` driver, or a minimal version: write a register,
   read it back, and confirm it matches. Reset the codec, then run the driver's init sequence with
   `adc_input = LINE2` and `dac_output = LINE2` (line-in and headphone on this board).
3. **Confirm no errors** and no crash. The codec draws its clock from **MCLK on GPIO0**, so I2S must be
   running before audio works (stage 6).

**Pass:** register write/read-back works and the init completes.
**If it fails:** wrong I2C pins (stage 3), pull-ups (the module has 10 kΩ pull-ups already), or GPIO0 being
driven by something else.

---

## Stage 6 — Audio output (earpiece path)

**Goal:** a clean tone out of the headphone jack, and volume steps that behave.

1. **Start I2S** (BCLK/WS/data pins from stage 3; **MCLK on GPIO0**) and play a 1 kHz sine at 8 kHz sample
   rate. Plug headphones into the **headphone** jack. Expect a clean tone in both channels.
   *If silent:* check `dac_output = LINE2` (the headphone jack is LOUT2/ROUT2; LOUT1/ROUT1 is the amp
   path), the volume registers, and the MCLK signal on GPIO0 with a scope if you have one.
2. **Keep the speaker amps off** and confirm the speaker connectors stay silent (GPIO21 unused).
3. **Sample rates:** repeat at 16 kHz, 22.05 kHz and 44.1 kHz. Note which work and which sound clean.
   (The existing firmware runs the codec at 8 kHz; prompts may want a higher rate, see stage 8.)
4. **Volume steps:** implement the 10-step mapping from `initial_design.md`
   (`gain_dB = min + (k/11)·(max−min)`). Record the output amplitude at each step (recording on a phone or
   an audio interface, or by ear with a note). Check that steps sound **even**, the lowest step is audible,
   and the highest does not clip or distort. Note the values to narrow the wide starting range.
5. **Noise check:** with silence playing, listen for hiss, hum or pops when the DAC starts and stops.

**Pass:** clean tone at the working rates, even volume steps, no unwanted amp output.

---

## Stage 7 — Audio input (microphone path)

**Goal:** a clean signal from the line-in jack, isolated from the onboard mics.

1. **Line-in test first:** feed a tone from a phone into the **line-in** jack with `adc_input = LINE2`.
   Capture with I2S, and either loop it back to the headphones or compute its level. Expect a clean copy of
   the tone.
2. **Show the known bug on purpose:** with the onboard mics still connected (C18 and C20 fitted), talk near
   the board while the line-in is silent. If you hear or capture your voice, the mics are mixed into line-in
   as documented.
3. **Isolate the mics:** remove **C18 and C20** (or the mic parts). Repeat step 2; the voice should
   disappear. Record what you removed.
4. **Set the gain:** with the real **MAX9814 module**, determine the ADC gain that gives a good level
   without clipping. Note that the MAX9814 output is DC-biased; the board's line-in coupling capacitors
   (C11, C13) block that.
5. **Long cord test:** connect the MAX9814 through the actual **coiled 3-conductor cord** and check for
   noise, hum and pickup of the ring driver later (stage 11).
6. **Noise floor:** record 30 s of silence; note the noise level.

**Pass:** clean voice from the MAX9814 into LIN2/RIN2 with the onboard mics isolated, gain chosen.

---

## Stage 8 — SD card for voice prompts

**Goal:** store many higher-resolution recordings on the SD card and play them reliably.

**Rules for this stage:**
- **Never let the code format the card.** The existing SD code in this repo
  (`components/utility/sample.c`) sets `format_if_mount_failed = true`; do not reuse that setting.
- Leave the SD card **out** while flashing until step 3 proves it is safe.

1. **Set the DIP switches** for SD: **SW2 OFF** (leave DATA3 pulled up on the card side), **SW3 ON**
   (IO15 to CMD). Use **1-bit SDMMC mode** (needs only IO14, IO15, IO2), and mount a FAT32 card
   read-only first. The card's DATA lines are pulled up on the board.
2. **Mount and list files.** Then read and write a test file, and read it back and compare.
3. **Boot and flash with the card inserted.** Some boards refuse to boot with a card in the slot. **Fail**
   means we must document "remove the card to flash," or add a workaround. Check both **power-on** and
   **flashing** with the card present. (GPIO2, 12 and 15 have pull-ups on the SD side and are strapping
   pins.)
4. **Speed test:** measure sequential read speed in 1-bit mode. Voice at 44.1 kHz, 16-bit mono needs about
   90 KB/s, so the requirement is easily met; the goal is finding **latency** and **hiccups**.
5. **Playback:** stream a 16-bit mono WAV from the SD card to the codec at **16 kHz, 22.05 kHz and
   44.1 kHz** while the rest of the system idles. Listen for dropouts. Try streaming while Bluetooth is
   active (stage 9 repeats this).
6. **Scale test:** put **hundreds of files** on the card (the plan has many prompts and variants) and
   measure the time to open a file by name. Check that file names follow the clip IDs in
   `audio_clips.md`.
7. **Reliability:** cycle power 50 times with the card inserted; remove and reinsert it while running
   (card-detect is on IO34). Confirm no corruption.
8. **Card sizes and brands:** try at least two cards. The spec claims support for cards up to 64 GB.

**Decisions this stage feeds:** the recording sample rate and format for `audio_clips.md`; 1-bit vs 4-bit
mode (4-bit needs IO4, IO12 and IO13, which the GPIO budget cannot spare); whether to keep a tiny set of
built-in fallback prompts for when the card is missing.

**Pass:** reliable mount, playback at the chosen rate without dropouts, boots with the card in, hundreds of
files fine.

---

## Stage 9 — Bluetooth handsfree

**Goal:** pair with a real phone, place and receive a call, and pass voice audio both ways.

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

1. **Wire the contacts** to the chosen GPIOs with pull-ups (stage 4), the 10–100 Ω series resistors and, if
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
| USB serial and flashing (method documented) | 1 | |
| Our own code runs; console output visible | 2 | |
| Module variant and I2C pins identified | 3 | |
| PSRAM (about 8 MB) | 3 | |
| GPIO plan proven, boot-safe, works with SD | 4 | |
| Codec control over I2C | 5 | |
| Earpiece audio out, chosen sample rates, even volume steps | 6 | |
| Microphone in (MAX9814), onboard mics isolated, gain chosen | 7 | |
| SD card: mount, boot-safe, high-resolution playback, hundreds of files | 8 | |
| Bluetooth: pairing (passkey), calls, two-way voice | 9 | |
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
| Clip format and sample rate; SD 1-bit vs 4-bit; fallback prompts | stages 6 and 8 |
| Volume ranges (the wide starting values) | stage 6 |
| Echo canceller kept or dropped | stage 9 |
| Ring frequency, voltage, cadence | stage 11 |
| Battery cutoff and whether to read battery voltage | stage 12 |

## Results log template

Copy this into a log file (for example `doc/validation_log.md`) and fill it in.

| Date | Stage | Step | What I did | Result (pass/fail) | Notes, values, photos |
|---|---|---|---|---|---|
| | | | | | |
