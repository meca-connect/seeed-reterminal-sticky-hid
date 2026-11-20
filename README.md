# Seeed reTerminal Sticky HID research

> **Current app: [Sticky Trackpad](docs/STICKY_TRACKPAD.md)** — BLE trackpad + keyboard + numpad (`sketches/s13_app`). Start there for features, gestures, build/flash and the roadmap. The notes below are the research log.

A project to explore the touch input on the Seeed Studio reTerminal Sticky, then adapt the input-to-HID approach from a related M5Stack PaperS3 project.

## Project status

- Target identified from the request as reTerminal Sticky (ESP32-S3), not a reTerminal E-series device.
- Phase 1: touch capability research. No HID reports or display driver are included yet.
- First diagnostic sketch: `touch_probe/touch_probe.ino`. It only initializes the touch rail/controller, reports raw GT911 contact data and interrupt-pin changes over serial. It does not send HID events or modify stored user data.
- The diagnostic is uploaded with esptool hash verification. Live UART0 confirms GT911 product ID accepted at 0x5D, actual DOWN/MOVE/UP contact frames, and repeated three-contact frames with distinct IDs 0, 1 and 2. Completed capture contains 109 parsed three-contact frames and 161 two-contact frames; final heartbeat has zero status-read errors. Orientation, maximum contact count, contact-ID continuity and hardware report rate remain unverified.
- The supplied Seeed Quick Start confirms expected user-facing gestures: tap to open/select/confirm, horizontal swipe for page navigation, inward edge swipe for back, and bottom-edge upward swipe for Home. These are interaction descriptions, not measured controller reporting characteristics.
- An original firmware backup is stored locally under the ignored `backups/` directory. Flash dumps and device logs are intentionally excluded from version control.

## Hardware facts from Seeed documentation

- Main controller: ESP32-S3, 8 MB PSRAM; 32 MB QSPI flash is reported by the Playground starter documentation.
- Touch: capacitive panel, Goodix GT911 (supported by Seeed's Sticky peripheral documentation and public Sticky firmware examples).
- Touch I2C: SDA GPIO3, SCL GPIO2; INT GPIO21; RESET GPIO41; touch power enable GPIO42.
- Display: 800 x 480, black-and-white e-paper, 4-level grayscale.
- Physical controls: AI/power GPIO4, Up GPIO5, Down GPIO6.
- USB-C includes a USB-to-UART/debug path; firmware USB/HID identity and serial/debug access must be checked separately.
- Schematic revision 01 sheet 8 confirms stock USB-C D+/D- connect to CH343P pins 7/8, not ESP32-S3 native USB. Stock-port plug-and-play USB HID is not a firmware-only option. BLE HID, serial plus a host companion (not USB HID), or an added native-USB bridge are separate implementation routes. Schematic preserved in `docs/hardware/`.

## Verified geometry (measured, not assumed)

The display is physically PORTRAIT (480 wide x 800 tall) with USB-C at the bottom;
the SSD1677's native 800x480 frame is mounted rotated 90 degrees. Touch raw
coordinates equal logical portrait coordinates directly, and the display needs a
90-degree transpose. Full calibration record and evidence chain: `docs/GEOMETRY.md`.

    touch:  logical_x = raw_x,  logical_y = 799 - raw_y   (raw y rises toward the top)
    display: logical (x,y) -> SSD (X = y, Y = x), sent unmirrored

Incremental sketches under `sketches/` (s1 blink, s2 map, s2b 4-corner calibrate,
s3 mirror, s4 dot-follow) verified this end to end; the shared driver is
`sketches/_common/sticky_hw.h`. The original one-shot `sticky_trackpad/` app
assumed landscape and is superseded pending a rebuild on the portrait model.

## PaperS3 reference

The related PaperS3 project is available at https://github.com/meca-connect/m5papers3-trackpad. It uses a GT911 touchscreen, BLE HID and USB HID, with a transport-independent gesture layer. Its hardware-specific BLE pairing logic and display/touch initialization must not be copied blindly; first capture Sticky touch data and verify host USB behavior independently.

## Build

Arduino CLI with Espressif ESP32 core 3.3.11 is installed locally. The Seeed Playground Arduino starter pins this core and the stock ESP32-S3 board profile. Sticky's flash/PSRAM options are set explicitly here:

```sh
arduino-cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio' --build-path build/touch_probe touch_probe
```

Compilation is not evidence that the device has been flashed or that the wiring/controller assumptions work on the physical unit. Do not upload until the exact device/revision and current firmware/data-preservation needs are confirmed.

Serial output is UART0 through the onboard CH343P USB-to-UART bridge. `CDCOnBoot=default` deliberately keeps Arduino `Serial` on UART0; native USB CDC is not the debug connector. Original compile-only settings had this wrong. The Seeed peripheral demo also establishes board power hold on GPIO45/GPIO46 and reads runtime sensor resolution at 0x8146. The probe incorporates these findings, validates product ID `911`, and keeps polling disabled after failed identification. It reports DOWN/MOVE/UP contact-frame transitions plus an ALIVE heartbeat every two seconds. DOWN/UP describe the overall contact set, not per-ID transitions; size is the controller's raw size field, not measured pressure. Polling is every 20 ms; this is a configured interval, not a measured hardware report rate or latency.

The minimal test intentionally leaves the e-paper image unchanged. Watch serial rather than expecting the old factory screen to react. No Wi-Fi/BLE/HID or sleep is enabled. Do not unplug USB during flashing.

Host tests (synthetic fixtures, not hardware observations):

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Itests/fakes tests/test_probe.cpp -o build/test_probe && build/test_probe
g++ -std=c++17 -Wall -Wextra -Werror tests/test_touch_data.cpp -o build/test_touch_data && build/test_touch_data
python3 scripts/capture_serial.py /dev/ttyACM0 30 logs/touch-test.log
```

Discover the actual port before capture. Only one serial reader should own it. The capture helper configures 115200 baud without changing DTR/RTS. Flash backups, logs and downloaded references are local/Git-ignored; backups can contain credentials and must remain private.

## Live terminal visualization

Run `python3 scripts/touch_visualizer.py --port /dev/ttyACM0` in a normal Linux terminal. Close other serial monitors first. Displays raw finger IDs/positions, contact count and stale status without firmware changes or HID output. Ctrl+C exits. See `docs/TOUCH_VISUALIZER_AND_PTP.md` for replay, limitations and the separate Precision Touchpad feasibility assessment. Three contacts are a necessary but not sufficient PTP prerequisite; prior RP2350 research is not a Windows-validated implementation.

## Reference links

- Seeed Quick Start and documented basic gesture set: https://www.seeedstudio.com/sticky/docs/en/quick-start/
- Seeed Sticky hardware overview: https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/
- Seeed touch and app-event integration: https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-pages/
- Seeed source archive (power hold, pin definitions, GT911 runtime registers): https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_peripheral_demo.zip
- Seeed Sticky Playground firmware registry: https://github.com/Seeed-Projects/reterminal-sticky-playground-registry
- M5PaperS3 HID reference: https://github.com/meca-connect/m5papers3-trackpad

See `GAME_PLAN.md` for the staged investigation and today's touch test matrix.
