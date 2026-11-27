# ESP32 controller firmware

The default target, `esp32_phone_admin`, is [Option C: ESP32 phone administration](../firmware/phone_admin/README.md).
It starts on power-up, drives a TFT, and stores classroom records on an external SPI microSD module.

For the [UNO + ESP-32U dual-controller build](DUAL_HARDWARE.md#option-a-alternative-version-a-dual-controller),
build `uno_display` and `esp32_dual`. The UNO owns the TFT and buttons; the ESP32 runs Wi-Fi,
the API and classroom state. The existing framed UART2 protocol is unchanged. `esp32_solo` is the
alternate ESP32-only display build, and `esp32wroom32d` remains a legacy headless/status target.
Options A and B store classroom state in internal LittleFS; their setup is in the
[alternate hardware guide](DUAL_HARDWARE.md).

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
the photos. Match the actual GPIO labels to the [Option C wiring diagram](../hardware/wiring.html#phone-admin)
and power and program the board through its micro-USB input.

The `huge_app.csv` partition table ends at `0x400000` (4 MB). This is a build configuration, not
evidence that the photographed board has 4 MB flash. Verify capacity before any partition or
filesystem write. Current build sizes (after running the commands below) should be recorded here;
no physical board test is implied.

## 2. Build, flash, monitor

[Wire the TFT and microSD module and prepare the card](../firmware/phone_admin/README.md#prepare-the-card-and-flash)
before first boot. Set private build credentials in your Bash shell:

```bash
cd firmware
read -rsp "Private AP password (8+ characters): " SNTCH_AP_PASSWORD
echo
export SNTCH_AP_PASSWORD
read -rsp "Private teacher PIN (8+ digits): " SNTCH_TEACHER_PIN
echo
export SNTCH_TEACHER_PIN
pio run
pio run -t upload
pio device monitor -b 115200
```

PlatformIO defaults to `esp32_phone_admin` (Option C); it uses the generic `esp32dev` definition,
`esptool`, 460800 baud upload, and Arduino serial at 115200. The USB-UART bridge is onboard, but its
model and exact automatic bootloader behavior are not verified. Use the board's EN/BOOT buttons if
required.

The ESP32 target uses the configured partition table. Uploading writes a partition table; preserve/back up valuable flash
data before first use if the board may contain data written with a different partition
layout. Verify the photographed board has sufficient flash before upload. A full chip erase or
`uploadfs` is not needed for normal reflashing and can erase classroom data.

Option C does not use `uploadfs` or migrate existing records from LittleFS. For Options A and B,
follow their separate [build and storage instructions](DUAL_HARDWARE.md).

## 3. Configuration

All settings are build flags, overridable without editing code:

| Flag | Default | Notes |
|---|---|---|
| `SNTCH_AP_SSID` | `SNTCH-EXAM` | local classroom Wi-Fi name |
| `SNTCH_AP_PASSWORD` | required environment variable | private WPA2 password, minimum 8 characters |
| `SNTCH_AP_CHANNEL` | `1` | 1–13; matches the tested ESP32 reference setup |
| `SNTCH_AP_MAX_CLIENTS` | `10` | hardware ceiling for the ESP32 access point |
| `SNTCH_TEACHER_PIN` | required environment variable | private teacher PIN, minimum 8 digits; never logged or served |
| `SNTCH_FIRMWARE_VERSION` | `0.1.0` | reported by `/api/health` and the apps |
| `SNTCH_HTTP_PORT` | `80` | plain HTTP |
| `SNTCH_SAVE_INTERVAL_MS` | `60000` | checkpoint while an exam is running |
| `SNTCH_DIAG_INTERVAL_MS` | `30000` | serial diagnostics cadence |
| `SNTCH_UNO_STATUS` | `0` in Option C | the legacy `esp32wroom32d` target enables the one-way status link |
| `SNTCH_UNO_BAUD` | `9600` | legacy status-link baud, 8N1 |
| `SNTCH_UNO_TX_PIN` | `17` | legacy status-link UART2 TX; level-shift to Uno RX |
| `SNTCH_UNO_RX_PIN` | `16` | legacy target only; unused for its one-way link |
| `SNTCH_UNO_STATUS_INTERVAL_MS` | `2000` | legacy status-frame cadence |

Use the same environment values for each firmware build and upload in that shell. Do not put the
credentials in tracked build files or share the generated firmware image publicly.

### Legacy one-way Uno status link

Only the legacy `esp32wroom32d` target and its matching reference sketch use this link. The legacy
Uno reference sketch can act as a read-only wall
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
The Option A `esp32_dual` target uses framed, bidirectional UART2 with GPIO16/RX2 and
GPIO17/TX2, the UNO's A1/TX and A0/RX, and dedicated translators in both directions. See the
[Option A wiring guide](DUAL_HARDWARE.md#option-a-alternative-version-a-dual-controller). The
UNO's D0/D1 remain free for its USB connection.


## 4. Boot and runtime behaviour

1. Log the firmware version and initialize the TFT and shared SPI bus.
2. Mount the microSD card without formatting. Recover interrupted file replacements under `/sntch`.
3. Load `/sntch/state.json`. On a new card, `/sntch/sntch_pending_init.txt` authorizes initial
   state creation. A mount or load failure prevents the AP and API from starting, and the TFT
   shows **MICROSD UNAVAILABLE**.
4. Access point started, hostname `sntch`, IP `192.168.4.1`. If startup fails, the
   firmware reports an error and retries every 5 seconds; it does not print `ready` early.
5. Start the HTTP API on port 80. The TFT shows the controller QR and manual address when ready;
   it shows a student join QR and code when exactly one activity is live.
6. Loop: serve HTTP, advance exam clocks, checkpoint every 60 s while an exam is in progress,
   refresh the TFT, and print diagnostics every 30 s (uptime, heap, associated clients, students,
   open sessions, submissions, storage use). Tokens, PINs
   and answer text never appear in logs.

Expected volume: one boot block, one line every 30 s. Adjust `SNTCH_DIAG_INTERVAL_MS` if a serial
capture is noisy.

### Restart recovery

- Sessions that were `live` come back `paused` — nobody continues unsupervised work after a
  brownout.
- In-progress attempts are restored with their last checkpointed remaining time.
- Submissions already written to the card survive and keep their idempotency keys, so retries after
  a restart return the original grade.
- Pending deletions and their `/del/<sessionId>.json` backups survive, and the session stays hidden
  from students until the teacher app ACKs or cancels.
- Teacher and student tokens are RAM-only: the teacher signs in again, students re-join.

### Storage failure modes

| Condition | Behaviour |
|---|---|
| `/state.json` corrupt/incompatible | service does not start; serial log identifies the state load failure; file is preserved |
| Card missing, mount fails, or `/sntch` is absent | service does not start; TFT reports **MICROSD UNAVAILABLE** |
| Disk full mid-submission | submission file write fails → `500 STORAGE_FULL`, no partial summary recorded |
| Disk full during delete backup | `/del/` write fails → `500 STORAGE_FULL`, the session is not queued |
| Power loss during file replacement | the next mount reconciles `.sntch-new` and `.sntch-old` files; it cannot repair a corrupted FAT32 filesystem |
| Card I/O fails while serving a class | the classroom service stops; check the card and restart |

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
- Multi-teacher accounts
- Export endpoints
