# SNTCH — offline-first classroom controller

SNTCH is a classroom activity, attendance, examination and session system that runs on a
standalone **ESP-32U** controller with a TFT, an external SPI microSD module, and **Android**
teacher and student apps. The controller
creates its own Wi-Fi network; nothing depends on the internet, a Raspberry Pi, a router or a
cloud service.

This repository contains a working V1 implementation, not a prototype. The supplied
`sntch-visual-design.html` remains the visual reference; the Android apps recreate its Forest Tech
language in Jetpack Compose.

## Layout

```
sntch-visual-design.html      visual reference (unchanged)
firmware/                     ESP32 PlatformIO project (Arduino framework; WROOM-32D target)
  lib/sntch_core/             portable domain + HTTP/JSON API, no Arduino dependencies
  src/                        AP, HTTP glue, LittleFS storage, diagnostics
  test/core_tests.cpp         native test suite for core + API
android/                      Android app (teacher + student roles in one APK)
  app/src/main/java/...       protocol client, repositories, Compose UI
  app/src/test/java/...       JVM tests against a loopback controller
tools/host_sim/               host HTTP server that runs the same core
tools/e2e_test.py             end-to-end HTTP test, including restart recovery
tools/capture_fixtures.py     records real controller payloads for the Android wire tests
docs/                         architecture, protocol, firmware, Android, testing, status
```

**User guides:** [Visual offline guide](docs/SNTCH_VISUAL_USER_GUIDE.html) ·
[Markdown guide](docs/SNTCH_USER_GUIDE.md)

`Freebuff Creation/` is not part of this implementation and was left untouched.

## Quick start

**Firmware:** PlatformIO defaults to Option C (`esp32_phone_admin`). Follow the
[wiring diagram](hardware/wiring.html#phone-admin) and [setup guide](firmware/phone_admin/README.md)
to wire the microSD module, prepare the FAT32 card, and set your private Wi-Fi password and
teacher PIN before building:

```bash
pio run -d firmware
pio run -d firmware -t upload
pio device monitor -d firmware -b 115200
```

Option C stores records on the card and does not need `uploadfs`. The board starts an access point
named `SNTCH-EXAM` and serves `http://192.168.4.1` once storage is ready. See the
[hardware guide](docs/DUAL_HARDWARE.md) for Options A and B.

**Android**:

```bash
export JAVA_HOME="C:/Program Files/Java/jdk-25"   # this machine's jdk-21 path is broken
cd android
./gradlew :app:testDebugUnitTest :app:assembleDebug
# app/build/outputs/apk/debug/app-debug.apk
```

**No hardware?** Run the simulator and the whole protocol end to end:

```bash
bash tools/native_build.sh          # builds core tests + host sim, runs both suites
# or manually:
python tools/e2e_test.py            # 79 HTTP checks, including a restart
```

The Android app can point at `http://<this-PC-IP>:8080` when the phone is on the same network
as the simulator, but the simulator is a test harness, not an access point.

## Verified status (short version)

| Item | Status |
|---|---|
| Portable core + API | implemented, compiled, 284 native checks passing |
| HTTP end-to-end teacher/student workflows | verified over real sockets, 79 checks, restart included |
| ESP32-WROOM-32D firmware | compiles for `esp32dev` (4 MB); **not flashed or run on hardware** |
| Android app | compiles to a debug APK, 33 JVM tests passing; **not installed on a device** |
| GitScribe MCP | unavailable in this environment; see `docs/STATUS.md` |

Known ceiling: the configured ESP32 access point supports up to 10 stations, so one controller can
serve about 8–10 simultaneously connected phones even though the data model holds 50 students.
`docs/FIRMWARE.md` explains the options.

See `docs/STATUS.md` for the precise implemented/compiled/tested matrix and remaining work.
