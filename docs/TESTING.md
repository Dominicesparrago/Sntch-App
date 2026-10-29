# Testing and verification

Everything below was run in this environment. Nothing here is a projection.

## 1. Suites and results

| Suite | Command | Result |
|---|---|---|
| Core + API (native) | `bash tools/native_build.sh` | **373 checks, 0 failures** |
| HTTP end-to-end incl. restart | same script (or `python tools/e2e_test.py`) | **101 checks, 0 failures** |
| Android JVM (models, transport, repositories) | `bash tools/android_build.sh` | **37 tests, 0 failures** |
| Firmware build, ESP32-WROOM-32D | `pio run -e esp32wroom32d` | **SUCCESS**, RAM 48,200 / 327,680 B (14.7%), flash 1,384,028 / 3,145,728 B (44.0%) |
| Uno status-link compile | `pio run -e uno --project-dir .build/uno_check` | **SUCCESS**, RAM 1,009 / 2,048 B (49.3%), flash 31,996 / 32,256 B (99.2%) |
| WROOM LittleFS image | `pio run -e esp32wroom32d -t buildfs` | **SUCCESS**, image includes pending first-use marker |
| WROOM TFT/button harness | `cd firmware/esp32_wroom32d_tft_buttons && pio run` | **SUCCESS**, RAM 22,388 / 327,680 B (6.8%), flash 324,036 / 1,310,720 B (24.7%) |
| Android APK build | `./gradlew :app:assembleDebug` | **SUCCESS**, `app-debug.apk` (~9.1 MB) |

### Environment notes

- `g++` from MSYS2 needs `/c/msys64/mingw64/bin` on `PATH`; otherwise `cc1plus` exits 127 with no
  message. `tools/native_build.sh` sets this.
- `JAVA_HOME` points at a missing `jdk-21` on this machine. Use
  `C:/Program Files/Java/jdk-25` (the wrapper script does).

## 2. What the native suite covers

`firmware/test/core_tests.cpp` (373 checks):

- JSON: round-trip, unicode escapes, malformed input, trailing data, depth limit.
- Roster: validation, upsert by student number, 50-student capacity.
- Activities/exams: draft → publish, question validation (MC options/correct index, text keys),
  duplicate open session refusal, section targeting.
- Sessions: code format/uniqueness, ready → live → paused → resume → end, expiry, idempotent
  start.
- Join: section mismatch, unknown code, idempotent rejoin (membership stays 1, new token),
  one-session-at-a-time, switching sessions moves membership and invalidates the old token, an
  in-progress exam blocks switching, leave.
- Attendance: mark-all, single mark, wrong-section rejection, date validation, clearing removes
  the day.
- Exams: start gating by live session, answer keys absent from student payloads, autosave
  validation (unknown question id rejected), pause freezes the clock, incomplete submit rejected,
  grading with case/whitespace normalisation, pass mark, duplicate submit returns the original,
  submitted exam cannot restart.
- Timeout and session end: auto-submission from autosaved answers, `timeout: true`, score correct.
- Persistence: restart restores roster/sessions/submissions, `live` downgrades to `paused`,
  tokens do not survive, autosaved answers survive and grade after rejoin.
- Storage: full disk produces `STORAGE_FULL`; explicit first-use marker setup, installed-marker
  missing-state detection, unmarked filesystems, malformed/unsupported state, and interrupted
  initialization recovery.
- Deletion lifecycle: ready session queues with a backup file, students are gated out, backup is
  readable before ACK, pending deletion and its backup survive a restart, repeated ACK is
  idempotent, cancel restores the previous state, `live`/`paused` sessions are refused, ended
  sessions with participants back up their membership, and sessions with submissions are refused.
- Uno status line: empty classroom reports `none`/`-----`, a ready session reports its code, and a
  live session with one joined student increments the live/joined fields.
- API: auth failures, 413 oversized body, 404 unknown route, per-IP rate limiting, full teacher →
  student workflow through the router, submission detail (prompts present, keys absent) and the
  deletion routes.

The last item is a regression test for a genuine crash found by the E2E run: the detail endpoint
held a pointer into a temporary JSON document.

## 3. What the end-to-end suite covers

`tools/e2e_test.py` starts the host simulator, drives it with real HTTP over loopback, kills it,
restarts it against the same data directory, and continues (101 checks):

- health, teacher auth (wrong and right PIN), roster import idempotency
- draft item cannot open a session; publish then create/start session; public code preview
- student join/rejoin, cross-section refusal, student snapshot
- activity submit, retry dedup, teacher review, stored text retrieval
- attendance mark-all + individual update
- exam: start, no answer keys on the wire, autosave, paused submission refused, resume, grading,
  duplicate retry, teacher detail includes prompts without keys, restart refusal
- second section isolation: a student cannot read another student's submission
- deletion: sessions with submissions and live sessions are refused; a ready session queues with
  a backup, students cannot preview/join it, the queue and backup endpoint agree, cancel restores
  `ready`; after a restart the pending session, backup and student gate survive, ACK removes the
  session and is idempotent
- validation: malformed JSON 400, 17 KB body 413, unknown endpoint 404, bad token 401
- restart: roster/submissions/attendance survive, teacher token does not, live session reloads
  paused, duplicate guard holds, in-progress attempt auto-submits when the teacher ends the session
- post-end retry is idempotent and adds no record

## 4. What the Android JVM suite covers

`android/app/src/test/java/com/sntch/app/data` (37 tests), against a real `HttpServer` on
127.0.0.1:

- `SntchApi`: JSON parsing, bearer header, POST body, error-envelope → `ApiException`
  (code/status/message), unreadable response and unreachable host → `NetworkException`,
  `ok:false` with HTTP 200 rejected.
- Models: teacher snapshot, items with questions, sessions/membership, submissions, null session,
  student attempt, submission detail question lookup, missing-field tolerance, pending deletions.
- Repositories: login persists the token; 401 clears it; roster upsert payload; exam payload
  shapes (correct index, options, keys); session action paths; attendance payload; student join
  persists identity and loads state; 401 forgets the session; exam submit payload/grading;
  autosave path; detail parse; offline error text; deletion downloads and verifies the backup
  before ACK; a failed local save prevents ACK; Retry and Cancel paths.
- `FakeStore` exercises the `LocalStore` interface, including draft persistence logic.
- `WireFixtureTest` parses payloads captured from the running controller, not hand-written
  samples: `python tools/capture_fixtures.py` drives the host simulator through login, roster,
  exam publish, join, autosave, graded submit, attendance and submission detail, and writes the
  raw responses to `android/app/src/test/resources/controller/`. A controller field rename or
  type change therefore fails the Android suite instead of surfacing as an empty screen on a
  phone. Regenerate the fixtures whenever the protocol changes.

## 5. Deliberately not tested here

| Item | Why | How to verify |
|---|---|---|
| Firmware on ESP32 hardware | no physical runtime test | flash the explicit matching environment, follow §6 |
| AP association and real Wi-Fi latency | no hardware | associate phones, watch `/api/health` and serial |
| UI rendering / gestures / on-device timing | no phone or usable emulator (only a TV AVD exists, not booted) | install the APK, run §7 |
| TLS / hostile-network security | not implemented | see security limitations in ARCHITECTURE.md |
| Long-term flash wear | needs days of runtime | monitor `storageUsed` and failures over a term |
| 40–50 simultaneous clients | the AP caps at 10 stations; claim avoided | run a rotation of ≤8 phones |

## 6. Manual hardware checklist (ESP32-WROOM-32D)

1. Read the module marking and confirm `ESP32-WROOM-32D`; check the carrier board exposes the
   `esp32dev` USB/UART and GPIOs. Do not flash the S3 environment to this board.
2. If this is a confirmed new device with no classroom data to keep, run
   `pio run -e esp32wroom32d -t uploadfs` once to write the pending first-use marker. This replaces
   the LittleFS partition. For an existing installation, skip this step.
3. From `firmware/`, run `pio run -e esp32wroom32d -t upload`; do not use a full-chip erase.
4. Open `pio device monitor -b 115200`. Expect boot, LittleFS mount, saved-state load, AP line,
   HTTP listener, then `ready`; no crash loop.
5. Join `SNTCH-EXAM` from a phone; open `http://192.168.4.1/api/health`. Expect
   `"ok":true`, `"storageError":false`, plausible `freeHeap`.
6. Sign in: `POST /api/teacher/login` with the configured PIN. Import two students, create and
   publish an activity, create a session, start it.
7. Join from the student app, submit a response; confirm it appears in teacher Results.
8. Run an exam for 2 minutes; pause, resume, end. Confirm timeout auto-submission if you let the
   clock run out.
9. Remove power mid-exam and restore it. Expect the session `paused`, the exam answer draft
   restored, and the teacher able to resume without duplicate submissions.
10. Watch the 30 s diagnostics line for heap stability (no steady decline) during a 10-minute
   session.

### Optional WROOM TFT/button smoke test

1. Confirm the TFT breakout's VCC/backlight requirements and 3.3 V logic compatibility before
   connecting it. The supplied sketch does not control a buzzer or backlight.
2. From `firmware/esp32_wroom32d_tft_buttons/`, run `pio run -t upload` and open serial at
   115200 baud.
3. Confirm the menu appears; test Select, Back, Left, and Right. This is a peripheral test only,
   not SNTCH API/session integration.

## 7. Manual Android checklist

1. `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`.
2. Connect the phone to `SNTCH-EXAM` (or to the simulator's LAN address).
3. Role gate → Test → teacher sign-in → roster import → publish → session → start.
4. On a second phone (or after switching roles) join with the code, answer, submit.
5. Put the phone in airplane mode mid-exam, type answers, re-enable Wi-Fi, confirm the draft is
   restored and submission succeeds once.
6. Kill and relaunch the app while joined: the session should still be active after re-join.
7. Delete an ended session; confirm the controller's queue clears only after the app reports the
   backup saved, and that the file exists under the app's `files/session-backups/` directory.
8. Rotate the screen on the dashboard and exam screens; confirm no state loss (activity is
   configured not to recreate on rotation).

## 8. Reproducing the runs

```bash
# core + simulator + end-to-end (clears .build/e2e-data each run)
bash tools/native_build.sh

# firmware image
cd firmware && pio run -e esp32wroom32d
cd firmware && pio run -e esp32wroom32d -t buildfs
cd firmware/esp32_wroom32d_tft_buttons && pio run

# Android tests and APK
bash tools/android_build.sh
```

The Uno compile in the table is a scratch check of the root `arduino-controls.ino` sketch, not a
tracked PlatformIO project. `.build/` is gitignored, so recreate it before reproducing that row:

```bash
mkdir -p .build/uno_check/src
cp arduino-controls.ino .build/uno_check/src/main.ino
cat > .build/uno_check/platformio.ini <<'EOF'
[env:uno]
platform = atmelavr
board = uno
framework = arduino
lib_extra_dirs = C:\Users\Dominic\Documents\Arduino\libraries
monitor_speed = 9600
EOF
pio run -e uno --project-dir .build/uno_check
```

`lib_extra_dirs` must point at an Arduino libraries folder containing `QRCode`, `Adafruit GFX` and
`Adafruit ST7735 and ST7789`. The sketch uses 99.2 % of the Uno's flash, so any addition needs a
re-measured budget.

Artifacts: `.build/core_tests.exe`, `.build/host_sim.exe`, `.build/e2e-state.json`,
`android/app/build/outputs/apk/debug/app-debug.apk`, `.build/uno_check/.pio/build/uno/firmware.elf`.
