# hwtest

Hardware validation firmware for the ESP32 Audio Kit V2.2. Companion to `doc/validate_board_plan.md`; it is
separate from the main firmware in `gcore_pots_bt/`. Currently implements **stage 2** (hello world) and **stage 3** (PSRAM and I2C scan). Later
stages add console commands here.

## Board and settings

Set in `sdkconfig.defaults`, matched to the board (ESP32-D0WD-V3 rev 3, Zbit 4 MB flash):

- Target `esp32`, flash 4 MB, DIO at 40 MHz.
- Console on UART0 at 115200 (through the CP2102).
- Minimum chip revision 3.
- PSRAM on, auto-detected (stage 3). If it is not found the app still boots and reports it.

## Prerequisites

- ESP-IDF v4.4.4 with the two workarounds from `doc/initial_design.md`: `setuptools<81` in the IDF Python
  environment, and `CMAKE_POLICY_VERSION_MINIMUM=3.5`.
- Board connected on the micro-USB port labeled **UART/POWER**, DIP switches all OFF.
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
- Exit the monitor with **Ctrl+]**. Ctrl+C does not exit; it is passed through to the board.
- Auto-reset works on this board; no BOOT/RESET button presses are needed.

## Monitor keys

| Keys                | Action                                                    |
|---------------------|-----------------------------------------------------------|
| **Ctrl+]**          | Exit the monitor                                          |
| Ctrl+T, then Ctrl+X | Exit (use this if `]` is awkward on your keyboard layout) |
| Ctrl+T, then Ctrl+H | Show the command menu                                     |
| Ctrl+T, then Ctrl+R | Reset the board (like RESET, without touching it)         |
| Ctrl+T, then Ctrl+F | Rebuild and flash, then keep monitoring                   |

If the terminal is stuck, close the tab, then run `lsof /dev/cu.usbserial-0001` to confirm nothing still holds
the port before flashing again.

## What to expect

1. A boot log ending in the app's info lines: chip (2 cores, revision 3), flash 4 MB, free heap, reset reason.
2. One line per second: `hello N  uptime T ms`.
3. Press RESET: the counter restarts and the reset reason reads "external pin".

## Stage 3: PSRAM and I2C scan

On every boot, before the hello lines, the app prints:

- `PSRAM chip size` (expect 8 MB) and `PSRAM in heap` (expect about 4 MB; the ESP32 maps only 4 MB, the
  rest needs himem). Then a 1 MB write/read-back test that ends in `PASS` or `FAIL`.
- An I2C scan of pair A (SDA 18 / SCL 23) and pair B (SDA 33 / SCL 32). The ES8388 answers at 0x10
  (the AC101 at 0x1A). The pair that answers decides the module variant and the pin plan.

Use DIP switches all OFF. GPIO0 (MCLK) is never touched. Press RESET to see the results again. Leave the
console at 1 line per second so the boot output is easy to read.

## Console stress test

Prints 100 numbered lines per second. Consecutive numbers make a dropped line visible as a gap.

```
idf.py menuconfig                # Hardware test -> Console lines printed per second -> 100
idf.py -p /dev/cu.usbserial-0001 -b 921600 flash monitor
```

Watch for a minute. Pass = no gaps and no garbled lines. Set the option back to 1 afterwards.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `Resource busy` opening the port | Another program holds it (QGroundControl, a second monitor). Find it with `lsof`. |
| No port under `/dev/cu.*` | Charge-only cable, wrong USB port, or missing CP210x driver. |
| `Failed to connect` | Hold BOOT, tap RESET, release BOOT, then retry. |
| Boot log complains about chip revision | `CONFIG_ESP32_REV_MIN` is higher than the chip's revision (this board is rev 3). |
| Boot log complains about flash size or partition table | Flash size in `sdkconfig` does not match the 4 MB chip. |
| Garbled monitor output | Console baud is not 115200. |
| Flash fails at 921600 | Drop to `-b 460800` and note it in `doc/validation_log.md`. |

Record results in `doc/validation_log.md`.
