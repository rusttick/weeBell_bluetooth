 1. Install ESP-IDF v4.4.4 (the version this project was built against — repo pins sdkconfig/dependencies.lock to it)
  git clone -b v4.4.4 --recursive https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh esp32

  2. Set up the environment (each new shell)
  . $HOME/esp-idf/export.sh

  3. Build
  cd gcore_pots_bt
  export CMAKE_POLICY_VERSION_MINIMUM=3.5
  idf.py build
  sdkconfig is already checked in and targets ESP32, so this should build as-is against your ELEGOO board's chip (same ESP-WROOM-32 silicon) — you won't need idf.py set-target.

  4. Flash
  idf.py -p /dev/tty.usbserial-XXXX flash
  (macOS port name will look like /dev/tty.usbserial-* or /dev/tty.SLAB_USBtoUART depending on the driver; ls /dev/tty.* with the board plugged/unplugged will tell you which.)

  5. Flash + watch logs
  idf.py -p /dev/tty.usbserial-XXXX flash monitor
  Ctrl+] exits the monitor.

  One caveat worth flagging now rather than after a failed build: this will compile and flash fine onto your bare ESP32, but at runtime it'll immediately hit the
  GUI/gcore/codec init code (LVGL display, gCore power protocol, ES8388 over I2C/I2S) that expects hardware your ELEGOO board doesn't have — so it'll likely crash or error out
  on first boot until that code is stripped or the hardware is attached. Want to tackle that next, or hold off until the SLIC/codec front end is sorted?
  
