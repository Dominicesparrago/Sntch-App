# SNTCH Hardware Compatibility — photographed ESP-32U development board

This document covers **Option A: Arduino UNO R3 + ESP-32U development board**. Option C
(`esp32_phone_admin`, with microSD) is the selected/default PlatformIO build. Use the
[interactive wiring blueprint](../hardware/wiring.html) and [hardware build guide](DUAL_HARDWARE.md).
The photos identify an ESP-32U board with a micro-USB connector, 38 header pins, EN/BOOT buttons,
onboard USB-UART/regulator components and an IPEX/U.FL-style antenna socket. They do **not** resolve
the exact carrier SKU, IC markings/ratings, flash size or antenna connector series. PlatformIO's
`esp32dev` is the generic classic-ESP32 build configuration, not a physical-board identification.
The `esp32_solo` profile is an alternate; the headless and one-way legacy target below remain for
existing installations.

## Option A UNO + ESP-32U build

`esp32_dual` builds the ESP32 half; `uno_display` builds the UNO panel. The UNO owns the TFT and
four buttons. The ESP32 board supplies Wi-Fi, HTTP/API, session state and QR data. The
existing framed UART2 protocol uses ESP32 GPIO16/RX2 and GPIO17/TX2 and UNO A1/TX and A0/RX at
9600 8N1. Confirm all printed pin labels before wiring; the diagram is not a physical header-order
template.

Power each controller through its own USB input (UNO USB-B, ESP32 micro-USB) and join grounds.
Never join the USB 5 V rails, connect UNO 5 V to an ESP32 GPIO, or feed a second 3.3 V source into
the board's 3V3 pin. The board's 3V3 output may supply translators/TFT only after its current
capacity is verified. Connect a 2.4 GHz, 50 Ω antenna with the exact mating connector before Wi-Fi
use; do not infer the antenna plug series from the photo.

### Logic-level translation

UNO GPIO is 5 V; ESP32 GPIO and the TFT interface are 3.3 V. The selected circuit has six 5 V →
3.3 V channels (five TFT signals plus UNO TX) and one 3.3 V → 5 V channel for ESP32 TX to UNO RX.
Common ground is required. Never bypass the translators.

| Direction | Selected part and setup | Datasheet-checked alternatives |
|---|---|---|
| UNO 5 V → ESP32/TFT 3.3 V | SN74LVC244A at 3.3 V; both active-low output enables asserted; confirm the exact suffix's 5 V-tolerant inputs | One 74LVC245A at 3.3 V, A→B, outputs enabled; one 74LVC541A at 3.3 V, both active-low enables asserted; or **two** 74LVC125A at 3.3 V, with used gates enabled |
| ESP32 3.3 V TX → UNO 5 V RX | SN74LVC1T45, VCCA=3.3 V, VCCB=5 V, A→B | One 74HCT125 / 74AHCT125 gate, or a 74HCT245/74AHCT245 configured A→B, powered at 5 V; HCT/AHCT TTL thresholds accept a 3.3 V high |

The 74LVC244A/245A/541A/125A families support 1.65–3.6 V supplies and inputs up to 5.5 V in
the listed TI datasheets; verify the manufacturer and exact ordering suffix for parts on hand.
The 74HCT/AHCT alternatives are 5 V-powered and have TTL-compatible inputs. Plain HC parts are
not equivalent. Set direction and output enables correctly, keep unused inputs defined, and leave
unused outputs open. Add a 100 nF bypass capacitor at U1 and one at each U2 supply pin (three total
for the selected LVC1T45 circuit).

The available 74LVC245 + 74HCT125D may be used as U1 + U2 only after the exact markings and
manufacturer datasheets confirm those electrical characteristics and pinouts. SOIC/TSSOP packages
need matching breadboard adapters. These are design checks, not physical validation.

## Standalone controller

The legacy headless environment is named `esp32wroom32d` and uses classic ESP32 board ID
`esp32dev`; the environment name does not identify the photographed module as WROOM-32D. Its
partition configuration assumes 4 MB flash.

This legacy firmware runs SNTCH V1 as a standalone controller. It creates the local `SNTCH-EXAM` Wi-Fi
access point (configured WPA2 password, channel 1), serves the existing SNTCH HTTP API, coordinates classroom
sessions, and stores state in LittleFS. The Uno and TFT are not required for classroom operation.
The Uno reference sketch remains preserved and can optionally show ESP32 status from a one-way UART
feed; it cannot send commands back to SNTCH.

## Separate classic ESP32 TFT/button test

The separate `firmware/esp32_wroom32d_tft_buttons/` project tests the ST7735 display and four
buttons directly on a classic ESP32. Its proposed wiring is:

| Signal | ESP32 GPIO |
|---|---:|
| TFT SCK / MOSI | 18 / 23 |
| TFT CS / DC / reset | 22 / 21 / 17 |
| TFT MISO | Not connected |
| Select / Back / Left / Right buttons | 32 / 33 / 25 / 26 |

Each button connects its GPIO to GND when pressed; inputs use `INPUT_PULLUP`. This avoids
flash-connected GPIO6–GPIO11 and boot-strapping GPIO12. The test initializes SPI on GPIO18/23.
It does not add display support to the SNTCH classroom firmware and does not control a buzzer or
software backlight.

The exact carrier-board pin labels and TFT breakout's VCC/backlight circuitry are not identified.
Check the carrier pinout and confirm 3.3 V-compatible display logic and power
requirements before wiring. The pin map has compiled but has not been physically tested.

## Legacy Uno read-only status link

Only the legacy headless ESP32 target sends this status line on UART2 TX GPIO17 at 9600 8N1. It never receives, so the Uno cannot
change sessions, grades, or configuration. The Uno R3 is a 5 V board: put a 3.3 V-to-5 V level
shifter between ESP32 GPIO17 and Uno D0/RX, connect a common ground, and leave Uno D1/TX
disconnected. Disconnect the ESP32 TX jumper while uploading the Uno sketch, because the ESP32 and
the USB-UART both drive D0.

| Signal | Connection |
|---|---|
| ESP32 GPIO17 TX (3.3 V) | level-shifter low-voltage input |
| Level shifter high-voltage output | Uno D0 RX (5 V) |
| GND | common ground |
| Uno D1 TX | not connected |

The `SNTCH-EXAM` reference TFT test project also lists GPIO17 as TFT reset. Those two uses conflict;
the Uno status link belongs to the main firmware, not that separate TFT test project. Reassign one
of them if both are ever combined. The link has compiled and its protocol is covered by native
tests, but the physical level-shifter wiring and Uno display have not been tested on hardware.

## Flash and first-use storage

`huge_app.csv` assumes 4 MB flash: a 3 MB application partition and an 896 KiB
LittleFS partition, with NVS/OTA metadata and coredump space. Keep the existing partition layout
when reflashing. Back up valuable flash data before changing partition settings.

Firmware mounts LittleFS with automatic formatting disabled. A mount failure, missing state with an
installed marker, malformed state, or unsupported state version prevents the AP and API from
starting. Existing valid markerless SNTCH state is adopted without changing the v1 classroom data.

For a confirmed blank device, build and upload the explicit first-use filesystem image:

```bash
cd firmware
pio run -e esp32_dual -t buildfs
# pio run -e esp32_dual -t uploadfs  # confirmed blank board only; replaces LittleFS
pio run -e esp32_dual -t upload
```

`uploadfs` replaces all contents of the filesystem partition. The image includes a pending marker;
firmware creates the empty classroom state and commits the initialized marker on first boot. Do not
run `uploadfs` on a device whose classroom files need to be preserved.

## Build and verification

- Export a private `SNTCH_AP_PASSWORD` (8+ characters) and `SNTCH_TEACHER_PIN` (8+ digits) in
  the shell before building the main firmware.
- Selected main firmware: `pio run -e esp32_dual -e uno_display`.
- ESP32 filesystem image: `pio run -e esp32_dual -t buildfs`.
- TFT/button test: run `pio run` in `firmware/esp32_wroom32d_tft_buttons/`.
- Portable core and HTTP/restart tests: `bash tools/native_build.sh`.
- Android JVM tests and debug APK: `bash tools/android_build.sh`.

Software builds and host tests do not verify the physical board, Wi-Fi range, display, buttons, or
Android-to-board communication. Confirm the module and carrier markings before upload, then test
serial logs, AP association, `/api/health`, teacher/student workflows, restart recovery, and the
optional TFT/button sketch on the actual hardware.
