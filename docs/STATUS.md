# Implementation status

Last verified in this environment: ESP32-WROOM-32D firmware build, 373 native checks, 101 HTTP
end-to-end checks, 37 Android JVM tests (including payloads captured from the live controller),
debug APK builds, and an Arduino Uno compile of the status-link sketch. **No ESP32 hardware, no Uno
hardware, and no Android device/emulator were used.** Anything below marked "host-tested" was
exercised in simulation, not on a phone or board.

## 1. Verification levels

- **Implemented** — code exists and is wired into the real flow.
- **Compiled** — builds with the documented toolchain.
- **Host-tested** — exercised by the native/E2E/JVM suites.
- **Hardware-tested** — run on a physical board or phone. **Nothing is at this level.**

## 2. Feature matrix

| Feature | Implemented | Compiled | Host-tested | Hardware |
|---|---|---|---|---|
| ESP32-WROOM-32D app, WPA2 access point, HTTP/JSON API | yes | yes | E2E (same core over TCP) | no |
| LittleFS persistence + atomic writes + boot recovery | yes | yes | native (memory storage, restart) | no |
| Teacher PIN sign-in, bearer tokens, role separation | yes | yes | native + E2E + JVM | no |
| Roster import/upsert, section targeting, 50-student limit | yes | yes | native + E2E + JVM | no |
| Activities: create draft, publish, complete | yes | yes | native + E2E | no |
| Examinations: builder, MC/text questions, grading, pass mark | yes | yes | native + E2E + JVM | no |
| Sessions: code, start/pause/resume/end/expiry, membership | yes | yes | native + E2E | no |
| Backup-before-delete with ACK/cancel and restart recovery | yes | yes | native + E2E + JVM | no |
| Student join/rejoin/leave, one session at a time | yes | yes | native + E2E + JVM | no |
| Activity responses + teacher review | yes | yes | native + E2E | no |
| Timed exams: server clock, autosave, timeout/auto-submit | yes | yes | native + E2E | no |
| Attendance marks + history (14 days) | yes | yes | native + E2E + JVM | no |
| Idempotent submission/retry after restart | yes | yes | native + E2E + JVM | no |
| Request validation, body/size/rate limits, answer-key hiding | yes | yes | native + E2E | no |
| Storage-full refusal (`STORAGE_FULL`) | yes | yes | native | no |
| Teacher UI (dashboard, sessions, roster, attendance, items, results, settings) | yes | yes (APK) | workbook logic only; no UI run | no |
| Student UI (join, home, activity, exam, results, profile) | yes | yes (APK) | repository logic only; no UI run | no |
| Offline draft restore + reconnect banners | yes | yes | repository/draft logic in JVM tests | no |
| Optional Uno read-only status over UART2 | yes | ESP32 + Uno | native status formatting; Uno compile only | no |
| Forest Tech visual language | yes | yes | not visually reviewed on device | no |

## 3. GitScribe MCP

**Not used for the work recorded here.** GitScribe MCP v1.0.0 exists out-of-tree at
`C:\Users\Dominic\Desktop\MCPs`. Earlier notes show its stdio server being started by hand and a
heartbeat confirmed, but the client could not discover or call its tools, so **no GitScribe tool was
used** to file plans, logs or documentation; see `docs/GITSCRIBE_MCP_VERIFICATION.md`.

In the session that produced the deletion lifecycle and the counts above, no `gitscribe-mcp` tool
was present in the session tool list, `gitscribe` was not on `PATH` or among installed npm globals,
and the project has no `.mcp.json`/`opencode.jsonc`. `AGENTS.md` asks for GitScribe before changes;
the task prompt asked to report the blocker and continue. These documents are the record. If you
provide the MCP command/path or permission to configure it, the work log can be pushed through
GitScribe on a later session; until then no GitScribe operation is claimed.

## 4. Known limitations

1. **Access-point capacity.** The configured ESP32-WROOM-32D access point allows at most 10 stations, so one controller serves
   ~8–10 phones, not 40–50. The data model holds 50. Options are in `docs/FIRMWARE.md` §5.
2. **No real-time clock.** No absolute scheduling, no wall-clock timestamps in results. Sessions
   expire by duration; attendance dates come from the teacher's phone.
3. **No TLS.** Bearer tokens travel on a WPA2 classroom LAN. Documented, not hidden.
4. **One attempt per exam**, single teacher PIN, no multi-teacher roles.
5. **Session deletion is backup-first**, but students, activities and submissions still have no
   destructive endpoints, and there is **no CSV export** in V1; the corresponding prototype
   buttons were removed rather than left non-functional.
6. **No QR scanning**; codes are typed. No camera permission is requested.
7. **No Wi-Fi auto-connect**; the user joins `SNTCH-EXAM` in system settings.
8. **UI unverified on a device.** No screenshot, layout or gesture claim is made.
9. **Hardware unverified.** The firmware image has never run on a board in this environment.
10. **Nonexistent scheduling UI**: the prototype's open/due window fields were dropped instead of
    faking a clock.
11. **Uno link unverified and tight.** The Uno status display compiles to 31,996 / 32,256 bytes
    (260 bytes free); no level-shifter wiring, UART timing, or display legibility has been tested.

## 5. Next steps, in order

1. **Flash a board** and run `docs/TESTING.md` §6. Fix anything discovered; re-run the suites.
2. **Install the APK on two phones** and run §7, especially pause/resume, reconnect and draft
   restore.
3. **Measure real AP behaviour** with 8 phones: association time, poll latency, heap over 30 min.
   Adjust `SNTCH_AP_MAX_CLIENTS` and document a supported class size.
4. Decide the capacity path: rotation, controller-per-room, or a station-mode/AP-bridge build.
5. Add a wall clock source (DS3231 RTC or a teacher-supplied time on session creation) before
   implementing scheduled open/due windows.
6. Small, safe additions: CSV export endpoint + share sheet, student delete, exam retake policy,
   QR display of the session code (display only, no camera needed).
7. Harden: per-student submission rate limit, optional AP password rotation, log redaction review,
   and a browser-based teacher fallback (explicitly out of V1 today).

## 6. What changed in this implementation

Created:

- `firmware/lib/sntch_core/` — JSON, storage abstraction, classroom domain, HTTP/JSON API
- `firmware/src/` — ESP32 boot, Wi-Fi AP, WebServer adapter, LittleFS storage, diagnostics
- `firmware/test/core_tests.cpp` — 373-check native suite
- `firmware/platformio.ini`
- `tools/host_sim/main.cpp` — simulator serving the same core
- `tools/e2e_test.py`, `tools/capture_fixtures.py`, `tools/native_build.sh`, `tools/run_sim.sh`,
  `tools/android_build.sh`
- `android/` — Gradle project, Compose teacher/student app, 37 JVM tests
- `docs/` — architecture, protocol, firmware, Android, testing, status
- `README.md`, `.gitignore`

Unchanged: `sntch-visual-design.html` (visual reference) and `Freebuff Creation/` (left untouched
per instruction). `AGENTS.md` untouched.
