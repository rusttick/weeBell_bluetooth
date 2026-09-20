# hwtest

Hardware validation firmware for the ESP32 Audio Kit V2.2. It is a serial console with one command per test,
separate from the main firmware in `gcore_pots_bt/`. **What to test, in what order, and what to expect is in
`doc/validate_board_plan.md`**; this file only describes the firmware. Record results in
`doc/validation_log.md`. Later stages add more commands.

## Files

| File | Contents |
|---|---|
| `main/main.c` | `app_main`: records the pin levels first, starts the console |
| `main/cmds_sys.c` | `info`, `psram`, `i2cscan`, `hello` |
| `main/cmds_gpio.c` | `pins`, `mode`, `set`, `watch`, `boot`, and the pin table |
| `main/cmds_codec.c` | `codec`: ES8388 over I2C, with the repo's init register sequence |
| `main/cmds_mic.c` | `mic`: line-in input levels and the input-path settings (left channel only, gains) |
| `main/cmds_audio.c` | `audio`, `tone`, `vol`, `volstep`, `mute`: I2S out to the codec with MCLK on GPIO0 |
| `main/cmds.h` | Shared declarations |
| `sdkconfig.defaults` | Board settings, below |

## Board and settings

Set in `sdkconfig.defaults`, matched to the board (ESP32-D0WD-V3 rev 3, Zbit 4 MB flash):

- Target `esp32`, flash 4 MB, DIO at 40 MHz.
- Console on UART0 at 115200 (through the CP2102).
- Minimum chip revision 3.
- PSRAM on, auto-detected. If it is not found the app still boots and reports it.

## Prerequisites

- ESP-IDF v4.4.4 with the two workarounds from `doc/initial_design.md`: `setuptools<81` in the IDF Python
  environment, and `CMAKE_POLICY_VERSION_MINIMUM=3.5`.
- Board connected on the micro-USB port labeled **UART/POWER**.
- **Quit QGroundControl** (or turn off its serial auto-connect). It grabs the port and esptool then fails with
  "Resource busy". Check with `lsof /dev/cu.usbserial-0001`.

## Build, flash and monitor

```
cd hwtest
. ~/src/esp-idf/export.sh
export CMAKE_POLICY_VERSION_MINIMUM=3.5
idf.py set-target esp32          # first time only
idf.py build
idf.py -p /dev/cu.usbserial-0001 -b 921600 flash monitor
```

- Use the `cu.*` port, not `tty.*`.
- The flash baud is a command-line flag (`-b`); it is not an `sdkconfig` option under `idf.py`. If 921600
  fails, try 460800.
- Auto-reset works on this board; no BOOT/RESET button presses are needed.

## Monitor keys

| Keys                | Action                                                                  |
|---------------------|-------------------------------------------------------------------------|
| **Ctrl+]**          | Exit the monitor. Ctrl+C does not exit; it is passed through to the board. |
| Ctrl+T, then Ctrl+X | Exit (use this if `]` is awkward on your keyboard layout)               |
| Ctrl+T, then Ctrl+H | Show the command menu                                                   |
| Ctrl+T, then Ctrl+R | Reset the board (like RESET, without touching it)                       |
| Ctrl+T, then Ctrl+F | Rebuild and flash, then keep monitoring                                 |

If the terminal is stuck, close the tab, then run `lsof /dev/cu.usbserial-0001` to confirm nothing still holds
the port before flashing again.

## Console commands

After a reset the monitor shows `hwtest>`. Type a command and press Enter. `help` lists them all.

| Command                               | What it does                                                                                                            |
|---------------------------------------|-------------------------------------------------------------------------------------------------------------------------|
| `info`                                | Chip, flash, heap, reset reason, uptime                                                                                 |
| `psram`                               | PSRAM size and a 1 MB write/read-back test (stage 3)                                                                    |
| `i2cscan`                             | Scan the two candidate codec I2C pin pairs (stage 3). Expect the ES8388 at 0x10 on SDA 33 / SCL 32.                     |
| `hello <per sec> <secs>`              | Console stress test (stage 2), for example `hello 100 60`                                                               |
| `pins`                                | Table of the test pins: mode, level now, level at boot                                                                  |
| `mode <pin> <in\|pu\|pd\|out> [0\|1]` | Set a pin: `in` = input, no pull; `pu` / `pd` = internal pull-up / pull-down; `out` needs a level                       |
| `set <pin> <0\|1>`                    | Change the level of a pin already in `out` mode                                                                         |
| `watch on\|off`, `watch <pin> ...`    | Print edges with a microsecond timestamp. A pin with over 200 edges a second is switched off (it is floating).          |
| `boot`                                | The pin levels captured at the start of `app_main`                                                                      |
| `codec regs\|rw\|init\|r\|w`          | ES8388 on SDA 33 / SCL 32 at 0x10: dump the registers, write/read-back test, reset and run the init sequence, `r <reg>` read, `w <reg> <val>` write (0x.. numbers) |
| `audio on [rate]\|off\|status` | Start or stop I2S (BCLK 27, WS 25, DOUT 26, MCLK on GPIO0). Rates 8000 to 48000. `audio out hp\|spk\|both` chooses the headphone jack (default), the speaker outputs (also drives IO21 high to enable the amps) or both. `audio pins <bck> <ws> <dout>` overrides the pins before the first `on`. |
| `tone <Hz> [dBFS]\|off`            | Sine tone on both channels, default -1 dBFS (near full scale), or silence                                             |
| `vol <dB>`                         | Net output gain, -91.5 to +4.5 dB. Default -40 dB.                                                                     |
| `volstep <0-9> [min max]`          | The 10-step volume model: digit 1 quietest, 0 loudest; default range -43.5 to +4.5 dB                                  |
| `eq lp <Hz>\|hp <Hz>\|hs <Hz> <dB>\|off` | Output filter: 2nd-order low-pass or high-pass, or a high shelf (negative dB cuts the highs). Prints its response at 60 Hz to 3.4 kHz. |
| `mute on\|off`                     | DAC mute                                                                                                              |
| `mic level [s]\|avg [s]\|snr [label]\|chan left\|both\|pga <0-8>\|gain <dB>\|gate on\|off\|status` | Line-in input (needs `audio on`): `avg` averages the left and right levels (and checks if right is a copy of left); `snr` is the guided silence/speech/silence signal-to-noise test; `level` is live; `chan left` powers down the right input and sends the left ADC to both slots; `pga` is 3 dB per step; `gain` is the ADC digital gain |

Only these pins can be touched: **IO13/MTCK, IO18, IO23, IO22, IO19, IO5, IO21** (IO21 is the speaker-amp enable, driven by `audio out spk`).
The `pins` table shows each one's role. Pins start as plain inputs with no pulls, so ones with nothing
attached (IO5, IO18, IO23) float and pick up mains hum.

## Troubleshooting

| Symptom                                                | Likely cause                                                                                                  |
|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|
| `Resource busy` opening the port                       | Another program holds it (QGroundControl, a second monitor). Find it with `lsof`.                             |
| No port under `/dev/cu.*`                              | Charge-only cable, wrong USB port, or missing CP210x driver.                                                  |
| `Failed to connect`                                    | Hold BOOT, tap RESET, release BOOT, then retry.                                                               |
| Boot log complains about chip revision                 | `CONFIG_ESP32_REV_MIN` is higher than the chip's revision (this board is rev 3).                              |
| Boot log complains about flash size or partition table | Flash size in `sdkconfig` does not match the 4 MB chip.                                                       |
| Garbled monitor output                                 | Console baud is not 115200.                                                                                   |
| Flash fails at 921600                                  | Drop to `-b 460800` and note it in `doc/validation_log.md`.                                                   |
| `watch` scrolls endlessly with one or more pins toggling | A floating input is picking up mains hum. Type `watch off`, or `mode <pin> pu`. |
| Ohmmeter readings leave a pin above 3.3 V              | An ohmmeter can charge a floating pin above VDD. Press its key, or reset the board, before reading voltages.  |
