# ESP32 controller firmware

## 1. Target and toolchain

| Item | Value |
|---|---|
| Board | ESP32-WROOM-32D on a compatible Dev Module carrier; PlatformIO ID `esp32dev`, 4 MB flash |
| Framework | Arduino (`framework = arduino`), PlatformIO platform `espressif32@8.0.0` |
| Build env | `esp32wroom32d` (sole hardware environment and default) |
| Filesystem | LittleFS on `spiffs` in `huge_app.csv`: 3 MB app, 896 KiB filesystem, NVS/OTA and coredump; table ends at 4 MB |
| Serial | 115200 baud |
| External libraries | none — JSON, storage and domain logic are in `lib/sntch_core` |

The firmware targets the available ESP32-WROOM-32D using PlatformIO's `esp32dev` board definition.
The `huge_app.csv` partition table ends at `0x400000`; firmware uses about 1.34 MB of its 3 MB
app partition.

Builds verified on this machine with PlatformIO Core 6.2.0:

```
WROOM-32D: RAM 48,200 / 327,680 bytes (14.7%); flash 1,384,028 / 3,145,728 bytes (44.0%)
```

No wiring is required for the core V1 controller beyond USB power. A TFT and Arduino Uno are not
required for classroom operation. The optional Uno link is described below. If you attach a TFT
directly to the ESP32, its GPIO signals are 3.3 V; check the exact breakout's VCC and backlight
requirements, and do not infer its supply voltage from the ST7735 controller name.

## 2. Build, flash, monitor

```bash
cd firmware
pio run -e esp32wroom32d           # compile
pio run -e esp32wroom32d -t upload # flash over USB
pio run -e esp32wroom32d -t buildfs # build first-use LittleFS image
pio device monitor -b 115200       # serial diagnostics
```

The default environment is `esp32wroom32d` and uses PlatformIO `esp32dev`, `esptool`, 460800 baud
upload, and Arduino serial at 115200.

This target uses the existing partition table. Uploading writes a partition table; preserve/back up valuable WROOM
flash data before first use if that board may contain data written with a different partition
layout. A full chip erase or `uploadfs` is not needed for normal reflashing and can erase classroom
data. Firmware mounts with `LittleFS.begin(false)` and never formats after a mount failure.
For a confirmed blank device, explicitly initialize its filesystem with
`pio run -e <target> -t uploadfs`. The image contains `firmware/data/sntch_pending_init.txt` to
authorize first-use initialization; the firmware writes an empty supported `/state.json`, commits
`/sntch_initialized.txt`, then removes the pending marker. `uploadfs` replaces the filesystem
partition and deletes any classroom files already there. A failed mount alone cannot tell a blank
partition from corruption or a partition-layout mismatch, so firmware logs the failure and remains
offline. Do not use `uploadfs` as a recovery attempt unless replacing the stored files is intentional.
On an existing installation, a valid legacy `/state.json` is adopted by writing the marker without
changing the classroom data. A missing state file with the installed marker, or a markerless
filesystem containing other files, stops startup for manual inspection.

## 3. Configuration

All settings are build flags, overridable without editing code:

| Flag | Default | Notes |
|---|---|---|
| `SNTCH_AP_SSID` | `SNTCH-EXAM` | matches the tested ESP32 and Uno reference setup |
| `SNTCH_AP_PASSWORD` | `sntch-1234` | WPA2, minimum 8 characters — **change before class** |
| `SNTCH_AP_CHANNEL` | `1` | 1–13; matches the tested ESP32 reference setup |
| `SNTCH_AP_MAX_CLIENTS` | `10` | hardware ceiling for the ESP32 access point |
| `SNTCH_TEACHER_PIN` | `246810` | teacher sign-in PIN; never logged or served |
| `SNTCH_FIRMWARE_VERSION` | `0.1.0` | reported by `/api/health` and the apps |
| `SNTCH_HTTP_PORT` | `80` | plain HTTP |
| `SNTCH_SAVE_INTERVAL_MS` | `60000` | checkpoint while an exam is running |
| `SNTCH_DIAG_INTERVAL_MS` | `30000` | serial diagnostics cadence |
| `SNTCH_UNO_STATUS` | `1` | transmit read-only status frames on UART2; `0` removes the code |
| `SNTCH_UNO_BAUD` | `9600` | status-link baud, 8N1 |
| `SNTCH_UNO_TX_PIN` | `17` | UART2 TX; wire through a level shifter to the Uno RX |
| `SNTCH_UNO_RX_PIN` | `16` | reserved; leave the Uno TX disconnected (link is one-way) |
| `SNTCH_UNO_STATUS_INTERVAL_MS` | `2000` | status frame cadence |

Example per-class build:

```bash
pio run -e esp32wroom32d \
  -D 'SNTCH_AP_PASSWORD="room9a-2026"' \
  -D 'SNTCH_TEACHER_PIN="918273"' \
  -D 'SNTCH_FIRMWARE_VERSION="0.1.0-9a"'
```

(The `-D` flags are passed through PlatformIO's build flags; alternatively add them to
`build_flags` in `platformio.ini`.)

### Optional Uno status link

The Uno reference sketch can act as a read-only wall display. The ESP32 transmits one line every
two seconds on UART2 (GPIO17 TX, 9600 8N1); it never reads commands back. The line is ASCII, not
JSON, so the nearly full Uno flash does not need a JSON parser:

```
SNT1,<students>,<open>,<live>,<paused>,<submissions>,<pending>,<state>,<code>,<joined>*<XOR>\n
```

`<state>` is `none` when no session is open, otherwise `live`, `paused`, `ready`, `ended`, or
`expired`. `<code>` is `-----` when there is no session. `<XOR>` is two uppercase hex digits: the
XOR of every byte before `*`. The Uno ignores frames with a bad checksum and shows `UART: waiting`
after six seconds without a valid frame.

Wiring is one-way and crosses voltage domains:

| Signal | Connection |
|---|---|
| ESP32 GPIO17 (3.3 V TX) | level-shifter 3.3 V side input |
| Level-shifter 5 V side output | Uno D0 (RX) |
| GND | common ground |
| Uno TX (D1) | leave disconnected |

Use a 3.3 V-to-5 V level shifter (for example a TXS0108E or a resistor divider suitable for
9600 baud). The Uno is 5 V and must not receive a bare 3.3 V signal as a reliable high; do not feed
5 V into the ESP32. Disconnect the ESP32 TX jumper while uploading a sketch over USB, because the
USB-UART and the ESP32 both drive Uno D0. The Uno's `Sync` menu item opens the status screen.


## 4. Boot and runtime behaviour

1. Serial banner with firmware version.
2. LittleFS mount without formatting; `/sub` and `/att` are created if missing (`/del` is created
   on the first deletion backup).
3. Classroom state loaded from `/state.json`. A pending marker from an explicitly uploaded fresh
   filesystem authorizes creation of the initial state. A zero-byte,
   malformed, unknown-version, or structurally incomplete state file stops startup without rewriting it.
   A mount or load failure prevents the AP and API from starting.
4. Access point started, hostname `sntch`, IP `192.168.4.1`. If startup fails, the
   firmware reports an error and retries every 5 seconds; it does not print `ready` early.
5. HTTP API started on port 80. Root `/` returns a short plain-text status.
6. Loop: serve HTTP, advance exam clocks, checkpoint every 60 s while an exam is in progress,
   send the Uno status frame every 2 s when enabled, and print diagnostics every 30 s (uptime,
   heap, associated clients, students, open sessions, submissions, storage use). Tokens, PINs
   and answer text never appear in logs.

Expected volume: one boot block, one line every 30 s. Adjust `SNTCH_DIAG_INTERVAL_MS` if a serial
capture is noisy.

### Restart recovery

- Sessions that were `live` come back `paused` — nobody continues unsupervised work after a
  brownout.
- In-progress attempts are restored with their last checkpointed remaining time.
- Submissions already written to flash survive and keep their idempotency keys, so retries after
  a restart return the original grade.
- Pending deletions and their `/del/<sessionId>.json` backups survive, and the session stays hidden
  from students until the teacher app ACKs or cancels.
- Teacher and student tokens are RAM-only: the teacher signs in again, students re-join.

### Storage failure modes

| Condition | Behaviour |
|---|---|
| `/state.json` corrupt/incompatible | service does not start; serial log identifies the state load failure; file is preserved |
| LittleFS mount fails | service does not start; firmware does not format the partition |
| Disk full mid-submission | submission file write fails → `500 STORAGE_FULL`, no partial summary recorded |
| Disk full during delete backup | `/del/` write fails → `500 STORAGE_FULL`, the session is not queued |
| Power loss during write | `*.tmp` + rename keeps the previous file intact |

## 5. Capacity: the honest number

The data model supports 50 students, 4 open sessions and 400 submissions. The **radio** does not:
the configured ESP32 access point associates at most 10 stations, and in practice 8 reliable clients is
the working figure. A 40–50 student classroom needs one of:

- rotation groups of ≤8 devices on one controller,
- one controller per section/room,
- a future station-mode build attached to an external access point (out of V1 scope).

Do not claim 50 simultaneous clients. Classroom-sized *data* fits comfortably; classroom-sized
*radio* does not.

Other bounded resources: 400 stored submissions, 20 questions per exam, 16 KiB request bodies,
120 requests/minute per IP. Exam answer autosaves are capped at 2 KiB each.

## 6. Verifying a running controller

From a phone or laptop joined to `SNTCH-EXAM`:

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
