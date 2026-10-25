# Testing and verification

Everything below was run in this environment. Nothing here is a projection.

## 1. Suites and results

| Suite | Command | Result |
|---|---|---|
| Core + API (native) | `bash tools/native_build.sh` | **222 checks, 0 failures** |
| HTTP end-to-end incl. restart | same script (or `python tools/e2e_test.py`) | **79 checks, 0 failures** |
| Android JVM (models, transport, repositories) | `bash tools/android_build.sh` | **33 tests, 0 failures** |
| Firmware build | `pio run -e esp32s3` | **SUCCESS**, RAM 46,928 / 327,680 B (14.3%), flash 1,275,416 / 3,145,728 B (40.5%) |
| Android APK build | `./gradlew :app:assembleDebug` | **SUCCESS**, `app-debug.apk` (~9.1 MB) |

### Environment notes

- `g++` from MSYS2 needs `/c/msys64/mingw64/bin` on `PATH`; otherwise `cc1plus` exits 127 with no
  message. `tools/native_build.sh` sets this.
- `JAVA_HOME` points at a missing `jdk-21` on this machine. Use
  `C:/Program Files/Java/jdk-25` (the wrapper script does).

## 2. What the native suite covers

`firmware/test/core_tests.cpp` (222 checks):

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
- Storage: full disk produces `STORAGE_FULL` instead of silent loss.
- API: auth failures, 413 oversized body, 404 unknown route, per-IP rate limiting, full teacher →
  student workflow through the router, submission detail (prompts present, keys absent).

The last item is a regression test for a genuine crash found by the E2E run: the detail endpoint
held a pointer into a temporary JSON document.

## 3. What the end-to-end suite covers

`tools/e2e_test.py` starts the host simulator, drives it with real HTTP over loopback, kills it,
restarts it against the same data directory, and continues (79 checks):

- health, teacher auth (wrong and right PIN), roster import idempotency
- draft item cannot open a session; publish then create/start session; public code preview
- student join/rejoin, cross-section refusal, student snapshot
- activity submit, retry dedup, teacher review, stored text retrieval
- attendance mark-all + individual update
- exam: start, no answer keys on the wire, autosave, paused submission refused, resume, grading,
  duplicate retry, teacher detail includes prompts without keys, restart refusal
- second section isolation: a student cannot read another student's submission
- validation: malformed JSON 400, 17 KB body 413, unknown endpoint 404, bad token 401
- restart: roster/submissions/attendance survive, teacher token does not, live session reloads
  paused, duplicate guard holds, in-progress attempt auto-submits when the teacher ends the session
- post-end retry is idempotent and adds no record

## 4. What the Android JVM suite covers

`android/app/src/test/java/com/sntch/app/data` (33 tests), against a real `HttpServer` on
127.0.0.1:

- `SntchApi`: JSON parsing, bearer header, POST body, error-envelope → `ApiException`
  (code/status/message), unreadable response and unreachable host → `NetworkException`,
  `ok:false` with HTTP 200 rejected.
- Models: teacher snapshot, items with questions, sessions/membership, submissions, null session,
  student attempt, submission detail question lookup, missing-field tolerance.
- Repositories: login persists the token; 401 clears it; roster upsert payload; exam payload
  shapes (correct index, options, keys); session action paths; attendance payload; student join
  persists identity and loads state; 401 forgets the session; exam submit payload/grading;
  autosave path; detail parse; offline error text.
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
| Firmware on a physical ESP32-S3 | no board attached | flash with `pio run -t upload`, follow §6 |
| AP association and real Wi-Fi latency | no hardware | associate phones, watch `/api/health` and serial |
| UI rendering / gestures / on-device timing | no phone or usable emulator (only a TV AVD exists, not booted) | install the APK, run §7 |
| TLS / hostile-network security | not implemented | see security limitations in ARCHITECTURE.md |
| Long-term flash wear | needs days of runtime | monitor `storageUsed` and failures over a term |
| 40–50 simultaneous clients | the AP caps at 10 stations; claim avoided | run a rotation of ≤8 phones |

## 6. Manual hardware checklist (ESP32-S3)

1. `cd firmware && pio run -e esp32s3 -t upload`, then open the monitor.
2. Expect the boot banner, LittleFS mount, `/state.json` load (empty on first boot), AP line and
   `ready`. No crash loop.
3. Join `SNTCH-CLASS` from a laptop; open `http://192.168.4.1/api/health`. Expect
   `"ok":true`, `"storageError":false`, plausible `freeHeap`.
4. Sign in: `POST /api/teacher/login` with the configured PIN. Import two students, create and
   publish an activity, create a session, start it.
5. Join from the student app, submit a response; confirm it appears in teacher Results.
6. Run an exam for 2 minutes; pause, resume, end. Confirm timeout auto-submission if you let the
   clock run out.
7. Remove power mid-exam and restore it. Expect the session `paused`, the exam answer draft
   restored, and the teacher able to resume without duplicate submissions.
8. Watch the 30 s diagnostics line for heap stability (no steady decline) during a 10-minute
   session.

## 7. Manual Android checklist

1. `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`.
2. Connect the phone to `SNTCH-CLASS` (or to the simulator's LAN address).
3. Role gate → Test → teacher sign-in → roster import → publish → session → start.
4. On a second phone (or after switching roles) join with the code, answer, submit.
5. Put the phone in airplane mode mid-exam, type answers, re-enable Wi-Fi, confirm the draft is
   restored and submission succeeds once.
6. Kill and relaunch the app while joined: the session should still be active after re-join.
7. Rotate the screen on the dashboard and exam screens; confirm no state loss (activity is
   configured not to recreate on rotation).

## 8. Reproducing the runs

```bash
# core + simulator + end-to-end (clears .build/e2e-data each run)
bash tools/native_build.sh

# firmware image
cd firmware && pio run -e esp32s3

# Android tests and APK
bash tools/android_build.sh
```

Artifacts: `.build/core_tests.exe`, `.build/host_sim.exe`, `.build/e2e-state.json`,
`android/app/build/outputs/apk/debug/app-debug.apk`.
