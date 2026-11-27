# ESP32 controller firmware

For [Option C, ESP32 phone administration](../firmware/phone_admin/README.md), build
`esp32_phone_admin`. It starts on power-up and uses a TFT and a prepared microSD card.

For the [UNO + ESP-32U dual-controller build](DUAL_HARDWARE.md#option-a-alternative-version-a-dual-controller),
build `uno_display` and `esp32_dual`. The UNO owns the TFT and buttons; the ESP32 runs Wi-Fi,
the API and classroom state. The existing framed UART2 protocol is unchanged. `esp32_solo` is the
alternate ESP32-only display build, and `esp32wroom32d` remains a legacy headless/status target.
Options A and B store classroom state in internal LittleFS. The LittleFS setup and recovery
instructions below apply to those builds.

## 1. Target and toolchain

| Item | Value |
|---|---|
| Board | Photo-identified ESP-32U 38-pin USB development board; PlatformIO ID `esp32dev` is the generic classic ESP32 build target, not a confirmed carrier SKU |
| Framework | Arduino (`framework = arduino`), PlatformIO platform `espressif32@8.0.0` |
| Build env | `esp32_phone_admin` (Option C and PlatformIO default), `esp32_dual` (Option A), `esp32_solo`, `uno_display`; `esp32wroom32d` (legacy) |
| Filesystem | FAT32 microSD for Option C; otherwise LittleFS on `spiffs` in `huge_app.csv`. The flash layout ends at 4 MB; confirm board flash capacity before flashing. |
| Serial | 115200 baud |
| External libraries | `uno_display`, `esp32_solo` and `esp32_phone_admin` use Adafruit ST7735/GFX; domain logic remains in `lib/sntch_core` |

Photos show an ESP-32U module on a development board with micro-USB, EN/BOOT buttons, USB-UART
bridge, regulator and an IPEX/U.FL-style antenna socket. The USB-UART and regulator part markings,
regulator current rating, exact antenna connector series, and flash size cannot be confirmed from
the photos. GPIO16/RX2 and GPIO17/TX2 are the dual firmware UART pins. Match labels on the actual
board; the diagram is not a physical header-order template. Power and program the board through its
micro-USB input. Do not connect UNO 5 V to ESP32 GPIO or 3V3.

The `huge_app.csv` partition table ends at `0x400000` (4 MB). This is a build configuration, not
evidence that the photographed board has 4 MB flash. Verify capacity before any partition or
filesystem write. Current build sizes (after running the commands below) should be recorded here;
no physical board test is implied.

The UNO and TFT are required in the Option A dual-controller profile. The alternate `esp32_solo`
connects the TFT directly to ESP32 3.3 V GPIO; check the exact breakout's VCC and backlight
requirements and do not infer its supply voltage from the ST7735 controller name.

## 2. Build, flash, monitor

```bash
cd firmware
read -rsp "Private AP password (8+ characters): " SNTCH_AP_PASSWORD
echo
export SNTCH_AP_PASSWORD
read -rsp "Private teacher PIN (8+ digits): " SNTCH_TEACHER_PIN
echo
export SNTCH_TEACHER_PIN
# Option C is the default target; prepare its microSD card before use.
pio run                                             # build Option C
# pio run -t upload                                # upload Option C after setup
# Option A is still available explicitly:
pio run -e esp32_dual -e uno_display                 # compile selected pair
pio run -e esp32_dual -t upload                      # ESP32 over board micro-USB
pio run -e uno_display -t upload                     # UNO over USB-B
pio run -e esp32_dual -t buildfs                     # build first-use LittleFS image
# pio run -e esp32_dual -t uploadfs                  # confirmed blank board only; replaces LittleFS
pio device monitor -b 115200       # serial diagnostics
```

PlatformIO defaults to `esp32_phone_admin` (Option C); it uses the generic `esp32dev` definition,
`esptool`, 460800 baud upload, and Arduino serial at 115200. The USB-UART bridge is onboard, but its
model and exact automatic bootloader behavior are not verified. Use the board's EN/BOOT buttons if
required.

The ESP32 target uses the configured partition table. Uploading writes a partition table; preserve/back up valuable flash
data before first use if the board may contain data written with a different partition
layout. Verify the photographed board has sufficient flash before upload. A full chip erase or
`uploadfs` is not needed for normal reflashing and can erase classroom data.

Options A and B (`esp32_dual` and `esp32_solo`) use LittleFS. Option C (`esp32_phone_admin`) stores
classroom records on microSD, has no LittleFS fallback, and does not need `uploadfs`. For a confirmed
blank Option A/B device, initialize LittleFS with
`pio run -e esp32_dual -t uploadfs` or `pio run -e esp32_solo -t uploadfs`. The image contains
`firmware/data/sntch_pending_init.txt` to
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
| `SNTCH_AP_PASSWORD` | required environment variable | private WPA2 password, minimum 8 characters |
| `SNTCH_AP_CHANNEL` | `1` | 1–13; matches the tested ESP32 reference setup |
| `SNTCH_AP_MAX_CLIENTS` | `10` | hardware ceiling for the ESP32 access point |
| `SNTCH_TEACHER_PIN` | required environment variable | private teacher PIN, minimum 8 digits; never logged or served |
| `SNTCH_FIRMWARE_VERSION` | `0.1.0` | reported by `/api/health` and the apps |
| `SNTCH_HTTP_PORT` | `80` | plain HTTP |
| `SNTCH_SAVE_INTERVAL_MS` | `60000` | checkpoint while an exam is running |
| `SNTCH_DIAG_INTERVAL_MS` | `30000` | serial diagnostics cadence |
| `SNTCH_UNO_STATUS` | `1` | legacy `esp32wroom32d` read-only status target; `esp32_dual` and `esp32_solo` set this to `0` |
| `SNTCH_UNO_BAUD` | `9600` | legacy status-link baud, 8N1 |
| `SNTCH_UNO_TX_PIN` | `17` | legacy status-link UART2 TX; level-shift to Uno RX |
| `SNTCH_UNO_RX_PIN` | `16` | legacy target only; unused for its one-way link |
| `SNTCH_UNO_STATUS_INTERVAL_MS` | `2000` | legacy status-frame cadence |

Set private values in the shell before building. PlatformIO reads them from the environment, so
they are not stored in the repository:

```bash
read -rsp "Private AP password (8+ characters): " SNTCH_AP_PASSWORD
echo
export SNTCH_AP_PASSWORD
read -rsp "Private teacher PIN (8+ digits): " SNTCH_TEACHER_PIN
echo
export SNTCH_TEACHER_PIN
pio run -e esp32_dual -e uno_display
```

Use the same environment values for each firmware build and upload in that shell. Do not put the
credentials in tracked build files or share the generated firmware image publicly.

### Legacy one-way Uno status link

Only the legacy `esp32wroom32d` target and its matching reference sketch use this link. It is not
the selected dual-controller protocol. The legacy Uno reference sketch can act as a read-only wall
display. The ESP32 transmits one line every
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

Use a 3.3 V-to-5 V level shifter for the legacy target; do not feed Uno 5 V into the ESP32.
The selected `esp32_dual` target instead uses framed, bidirectional UART2 with GPIO16/RX2 and
GPIO17/TX2, the UNO's A1/TX and A0/RX, and dedicated translators in both directions. See the
[Option A wiring guide](DUAL_HARDWARE.md#option-a-alternative-version-a-dual-controller). The
UNO's D0/D1 remain free for its USB connection.


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
   poll/serve framed UART2 requests in the dual target (or send a legacy status frame every 2 s
   when enabled), and print diagnostics every 30 s (uptime, heap, associated clients, students,
   open sessions, submissions, storage use). Tokens, PINs
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
     -H 'Content-Type: application/json' -d '{"pin":"<configured-teacher-pin>"}'
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
