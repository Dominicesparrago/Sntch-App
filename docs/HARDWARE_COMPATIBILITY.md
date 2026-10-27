# SNTCH Hardware Compatibility — ESP32-WROOM-32D

## Supported controller

The sole SNTCH hardware target is the classic ESP32 using the ESP32-WROOM-32D module. PlatformIO
uses environment `esp32wroom32d` and board ID `esp32dev`. The configured flash size is 4 MB.

The WROOM-32D runs SNTCH V1 as a standalone controller. It creates the local `SNTCH-EXAM` Wi-Fi
access point (`sntch-1234`, channel 1), serves the existing SNTCH HTTP API, coordinates classroom
sessions, and stores state in LittleFS. The Uno and TFT are not required for classroom operation.
The Uno reference sketch remains preserved and is an independent test; there is no implemented
ESP32-to-Uno protocol.

## Optional WROOM TFT/button test

The separate `firmware/esp32_wroom32d_tft_buttons/` project tests the ST7735 display and four
buttons directly on the WROOM-32D. Its proposed wiring is:

| Signal | WROOM-32D GPIO |
|---|---:|
| TFT SCK / MOSI | 18 / 23 |
| TFT CS / DC / reset | 22 / 21 / 17 |
| TFT MISO | Not connected |
| Select / Back / Left / Right buttons | 32 / 33 / 25 / 26 |

Each button connects its GPIO to GND when pressed; inputs use `INPUT_PULLUP`. This avoids the
module's flash pins GPIO6–GPIO11 and boot-strapping GPIO12. The test initializes SPI on GPIO18/23.
It does not add display support to the SNTCH classroom firmware and does not control a buzzer or
software backlight.

The exact WROOM carrier-board pin labels and the TFT breakout's VCC/backlight circuitry are not
identified. Check the carrier pinout and confirm 3.3 V-compatible display logic and power
requirements before wiring. The pin map has compiled but has not been physically tested.

## Flash and first-use storage

`huge_app.csv` is configured for the WROOM's 4 MB flash: a 3 MB application partition and an 896 KiB
LittleFS partition, with NVS/OTA metadata and coredump space. Keep the existing partition layout
when reflashing. Back up valuable flash data before changing partition settings.

Firmware mounts LittleFS with automatic formatting disabled. A mount failure, missing state with an
installed marker, malformed state, or unsupported state version prevents the AP and API from
starting. Existing valid markerless SNTCH state is adopted without changing the v1 classroom data.

For a confirmed blank device, build and upload the explicit first-use filesystem image:

```bash
cd firmware
pio run -e esp32wroom32d -t buildfs
pio run -e esp32wroom32d -t uploadfs
pio run -e esp32wroom32d -t upload
```

`uploadfs` replaces all contents of the filesystem partition. The image includes a pending marker;
firmware creates the empty classroom state and commits the initialized marker on first boot. Do not
run `uploadfs` on a device whose classroom files need to be preserved.

## Build and verification

- Main firmware: `pio run -e esp32wroom32d`.
- Filesystem image: `pio run -e esp32wroom32d -t buildfs`.
- TFT/button test: run `pio run` in `firmware/esp32_wroom32d_tft_buttons/`.
- Portable core and HTTP/restart tests: `bash tools/native_build.sh`.
- Android JVM tests and debug APK: `bash tools/android_build.sh`.

Software builds and host tests do not verify the physical board, Wi-Fi range, display, buttons, or
Android-to-board communication. Confirm the module and carrier markings before upload, then test
serial logs, AP association, `/api/health`, teacher/student workflows, restart recovery, and the
optional TFT/button sketch on the actual hardware.
