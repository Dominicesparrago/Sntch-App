# SNTCH — offline-first classroom controller

SNTCH is a classroom activity, attendance, examination and session system that runs on a
standalone **ESP32-S3** controller with **Android** teacher and student apps. The controller
creates its own Wi-Fi network; nothing depends on the internet, a Raspberry Pi, a router or a
cloud service.

This repository contains a working V1 implementation, not a prototype. The supplied
`sntch-visual-design.html` is preserved untouched as the visual reference; the Android apps
recreate its Forest Tech language in Jetpack Compose.

## Layout

```
sntch-visual-design.html      visual reference (unchanged)
firmware/                     ESP32-S3 PlatformIO project (Arduino framework)
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

`Freebuff Creation/` is not part of this implementation and was left untouched.

## Quick start

**Firmware** (PlatformIO 6.2, ESP32-S3 devkit, USB):

```bash
cd firmware
pio run -e esp32s3                 # build (verified: RAM 14%, flash fits 3 MB app partition)
pio run -e esp32s3 -t upload       # flash, if a board is connected
pio device monitor -b 115200
```

The board starts an access point named `SNTCH-CLASS` (password `sntch1234`) and serves
`http://192.168.4.1`. Change the SSID, password, teacher PIN and channel with build flags —
see `docs/FIRMWARE.md`.

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
| Portable core + API | implemented, compiled, 222 native checks passing |
| HTTP end-to-end teacher/student workflows | verified over real sockets, 79 checks, restart included |
| ESP32-S3 firmware | compiles for `esp32-s3-devkitc-1`; **not flashed or run on hardware** |
| Android app | compiles to a debug APK, 33 JVM tests passing; **not installed on a device** |
| GitScribe MCP | unavailable in this environment; see `docs/STATUS.md` |

Known ceiling: an ESP32-S3 access point associates at most 10 stations, so one controller can
serve about 8–10 simultaneously connected phones even though the data model holds 50 students.
`docs/FIRMWARE.md` explains the options.

See `docs/STATUS.md` for the precise implemented/compiled/tested matrix and remaining work.
