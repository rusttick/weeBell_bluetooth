# Validation Log

Results for `doc/validate_board_plan.md`.

| Date | Stage | Step | What I did | Result (pass/fail) | Notes, values, photos |
|---|---|---|---|---|---|
| 2026-09-19 | 0 | 3 | `idf.py --version` | pass | ESP-IDF v4.4.4. Harmless warnings: `pkg_resources` deprecated (setuptools<81 workaround) and Click `MultiCommand` deprecated. |
| 2026-09-19 | 0 | 1 | Photos | skipped | Skipped by choice. |
| 2026-09-19 | 1 | 1 | Found port | pass | `/dev/cu.usbserial-0001` (use `cu.*`, not `tty.*`). |
| 2026-09-19 | 1 | 2 | Measured 3V3 to GND | pass | 3.374 V (expect about 3.38 V). |
| 2026-09-19 | 1 | 3 | `esptool.py chip_id` (v3.3.2) | pass | ESP32-D0WD-V3, revision 3 (meets `CONFIG_ESP32_REV_MIN_3`). WiFi, BT, dual core, 240 MHz, 40 MHz crystal, MAC 70:4b:ca:24:f8:54, coding scheme None. First attempt failed: QGroundControl had the port open (quit it). |
| 2026-09-19 | 1 | 3 | `esptool.py flash_id` | pass | Manufacturer 0x5e (Zbit), device 0x4016, 4 MB (confirms 4 MB, not the main firmware's 16 MB). |
| 2026-09-19 | 1 | 5 | `espefuse.py summary` (read only) | pass | Factory state, nothing burned. XPD_SDIO_FORCE=False, so flash voltage is set by GPIO12 at reset (high = 1.8 V, low/floating = 3.3 V). Flash is a 3.3 V part, so GPIO12 must be low at boot: watch this with the SD card in (stage 8 step 3). JTAG_DISABLE=False, UART_DOWNLOAD_DIS=False, no flash encryption, no secure boot, WR_DIS=0, ADC_VREF=1128 mV, MAC CRC OK. |
| 2026-09-19 | 1 | 4 | Auto-reset | pass | Connected on its own, no manual BOOT/RESET needed. |
| 2026-09-19 | 1 | 6 | `chip_id` at 115200 and 921600 | pass | Both connect and switch baud cleanly. `chip_id` moves little data, so re-check 921600 with a real flash in stage 2. Stage 1 complete. |
| 2026-09-19 | 2 | 1-3 | Created `hwtest/`, built with `idf.py build` | pass (build only) | 4 MB, DIO 40 MHz, UART0 115200, min rev 3, PSRAM off. App 0x298e0 bytes. Not flashed yet. |
| 2026-09-19 | 2 | 3 | `idf.py -p /dev/cu.usbserial-0001 -b 921600 flash monitor` | pass | 170 KB app written in 1.5 s at 921600, hashes verified, so 921600 is reliable for flashing. |
| 2026-09-19 | 2 | 4 | Boot log and hello lines | pass | Bootloader: chip revision v3.1, DIO 40 MHz, 4 MB, partition table OK. App: 2 cores, WiFi/BT classic/BLE, free heap 299132 bytes, CPU 160 MHz (IDF default, not 240). `spi_flash: detected chip: generic` (Zbit uses the generic driver). Reset reason after the flash reset: power-on. One line per second, uptime steps of exactly 1000 ms. |
| 2026-09-19 | 2 | 5 | RESET button | pass | Counter restarted, as expected (reported by user; reset reason text not recorded). |
| 2026-09-19 | 2 | 6 | 100 lines/s console stress | pass | Sample pasted: lines 1158-1211, consecutive, uptime steps of exactly 10 ms, and line 1211 at 12154 ms (no drift, so no drops up to 12 s). Full minute reported "seems ok" but only about 12 s was pasted. Stage 2 complete. |
| 2026-09-19 | 3 | 1-2 | Flashed stage 3 `hwtest`, PSRAM check | pass | PSRAM: 64 Mbit device found, chip size 8 MB, heap 4194303 bytes (4 MB mapping limit; himem needed for the rest), 1 MB write/read test 0 mismatches (write 6.7 MB/s, read 10.0 MB/s at 40 MHz PSRAM). Boot memtest adds about 1 s to boot. Free heap with PSRAM 4490915 bytes. |
| 2026-09-19 | 3 | 3 | I2C scan, DIP all OFF | pass | **Pair B, SDA 33 / SCL 32: ES8388 at 0x10. Pair A, SDA 18 / SCL 23: no devices.** So this is the OLDER ES8388 module; GPIO 18, 23, 5 are free (after removing R68-R70). |
| 2026-09-19 | 3 | 4 | Updated `audio_kit_v2.2.md` sections 1, 4, 6, 9 and the plan's stage 4 | done | BCLK expected on IO27 (verify in stage 6). |
| | 0 | 2, 4, 5 | DIP all OFF, SD empty, battery off, serial driver, UART/POWER port | not reported | |
