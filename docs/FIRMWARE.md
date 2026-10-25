# ESP32-S3 controller firmware

## 1. Target and toolchain

| Item | Value |
|---|---|
| Board | `esp32-s3-devkitc-1` (PlatformIO board id), ESP32-S3 with 8 MB flash class module |
| Framework | Arduino (`framework = arduino`), PlatformIO platform `espressif32@8.0.0` |
| Build env | `esp32s3` (see `firmware/platformio.ini`) |
| Filesystem | LittleFS on the `spiffs` partition of `huge_app.csv` (3 MB app, ~1 MB data) |
| Serial | 115200 baud |
| External libraries | none — JSON, storage and domain logic are in `lib/sntch_core` |

Build verified on this machine with PlatformIO Core 6.2.0. Last build:

```
RAM:   14.3%  (46,928 / 327,680 bytes)
Flash: ~1.22 MB of the 3 MB application partition
```

No wiring is required for V1: the controller is a USB-powered board with a Wi-Fi antenna. A TFT,
an Arduino Uno, or any external peripheral is **not** part of this implementation, so there is no
voltage or pin assumption to get wrong. If you add a display later: the ESP32-S3 is 3.3 V logic,
the 5 V pin comes from USB VBUS, and level shifting is required for any 5 V device.

## 2. Build, flash, monitor

```bash
cd firmware
pio run -e esp32s3                 # compile
pio run -e esp32s3 -t upload       # flash over USB
pio device monitor -b 115200       # serial diagnostics
pio run -e esp32s3 -t erase        # wipe flash (also wipes classroom data)
```

The firmware formats LittleFS on first boot if the filesystem is absent, so `uploadfs` is not
needed for a fresh board. Reflashing the application does **not** erase `/state.json` unless the
partition table changes or you erase the chip.

## 3. Configuration

All settings are build flags, overridable without editing code:

| Flag | Default | Notes |
|---|---|---|
| `SNTCH_AP_SSID` | `SNTCH-CLASS` | network students join |
| `SNTCH_AP_PASSWORD` | `sntch1234` | WPA2, minimum 8 characters — **change before class** |
| `SNTCH_AP_CHANNEL` | `6` | 1–13; pick a quiet channel if the room is crowded |
| `SNTCH_AP_MAX_CLIENTS` | `10` | hardware ceiling for the ESP32 access point |
| `SNTCH_TEACHER_PIN` | `246810` | teacher sign-in PIN; never logged or served |
| `SNTCH_FIRMWARE_VERSION` | `0.1.0` | reported by `/api/health` and the apps |
| `SNTCH_HTTP_PORT` | `80` | plain HTTP |
| `SNTCH_SAVE_INTERVAL_MS` | `60000` | checkpoint while an exam is running |
| `SNTCH_DIAG_INTERVAL_MS` | `30000` | serial diagnostics cadence |

Example per-class build:

```bash
pio run -e esp32s3 \
  -D 'SNTCH_AP_PASSWORD="room9a-2026"' \
  -D 'SNTCH_TEACHER_PIN="918273"' \
  -D 'SNTCH_FIRMWARE_VERSION="0.1.0-9a"'
```

(The `-D` flags are passed through PlatformIO's build flags; alternatively add them to
`build_flags` in `platformio.ini`.)

## 4. Boot and runtime behaviour

1. Serial banner with firmware version.
2. LittleFS mount (format on first boot), `/sub` and `/att` created.
3. Classroom state loaded from `/state.json`. A missing file starts empty; an unparsable file is
   reported as `storageError` instead of crashing.
4. Access point started, hostname `sntch`, IP `192.168.4.1`.
5. HTTP API started on port 80. Root `/` returns a short plain-text status.
6. Loop: serve HTTP, advance exam clocks, checkpoint every 60 s while an exam is in progress,
   print diagnostics every 30 s (uptime, heap, associated clients, students, open sessions,
   submissions, storage use). Tokens, PINs and answer text never appear in logs.

Expected volume: one boot block, one line every 30 s. Adjust `SNTCH_DIAG_INTERVAL_MS` if a serial
capture is noisy.

### Restart recovery

- Sessions that were `live` come back `paused` — nobody continues unsupervised work after a
  brownout.
- In-progress attempts are restored with their last checkpointed remaining time.
- Submissions already written to flash survive and keep their idempotency keys, so retries after
  a restart return the original grade.
- Teacher and student tokens are RAM-only: the teacher signs in again, students re-join.

### Storage failure modes

| Condition | Behaviour |
|---|---|
| `/state.json` corrupt | starts empty, `/api/health` reports `storageError`, app shows a storage warning |
| LittleFS mount fails | runs with an in-memory-only storage object; mutations return `STORAGE_FULL` |
| Disk full mid-submission | submission file write fails → `500 STORAGE_FULL`, no partial summary recorded |
| Power loss during write | `*.tmp` + rename keeps the previous file intact |

## 5. Capacity: the honest number

The data model supports 50 students, 4 open sessions and 400 submissions. The **radio** does not:
one ESP32-S3 access point associates at most 10 stations, and in practice 8 reliable clients is
the working figure. A 40–50 student classroom needs one of:

- rotation groups of ≤8 devices on one controller,
- one controller per section/room,
- a future station-mode build attached to an external access point (out of V1 scope).

Do not claim 50 simultaneous clients. Classroom-sized *data* fits comfortably; classroom-sized
*radio* does not.

Other bounded resources: 400 stored submissions, 20 questions per exam, 16 KiB request bodies,
120 requests/minute per IP. Exam answer autosaves are capped at 2 KiB each.

## 6. Verifying a running controller

From a phone or laptop joined to `SNTCH-CLASS`:

```bash
curl http://192.168.4.1/api/health
curl -X POST http://192.168.4.1/api/teacher/login \
     -H 'Content-Type: application/json' -d '{"pin":"246810"}'
curl http://192.168.4.1/api/teacher/state -H 'Authorization: Bearer <token>'
```

A healthy response includes `"storageError": false`, the expected `students` count, and
`"freeHeap"` in the hundreds of kilobytes. If health works but the teacher app cannot sign in,
check the PIN build flag.

## 7. Not implemented in the firmware

- TLS/HTTPS (no certificate story on a local AP in V1)
- OTA updates (flash over USB)
- Real-time clock / internet time
- mDNS discovery (the address is documented and configurable in the app)
- Display integration
- Multi-teacher accounts
- Export endpoints
