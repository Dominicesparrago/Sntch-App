# Implementation status

Last verified in this environment: firmware builds, 222 native checks, 79 HTTP end-to-end checks,
33 Android JVM tests (including payloads captured from the live controller), debug APK builds. **No ESP32-S3 hardware and no Android device/emulator were
used.** Anything below marked "host-tested" was exercised in simulation, not on a phone or board.

## 1. Verification levels

- **Implemented** — code exists and is wired into the real flow.
- **Compiled** — builds with the documented toolchain.
- **Host-tested** — exercised by the native/E2E/JVM suites.
- **Hardware-tested** — run on a physical board or phone. **Nothing is at this level.**

## 2. Feature matrix

| Feature | Implemented | Compiled | Host-tested | Hardware |
|---|---|---|---|---|
| ESP32-S3 app, WPA2 access point, HTTP/JSON API | yes | yes | E2E (same core over TCP) | no |
| LittleFS persistence + atomic writes + boot recovery | yes | yes | native (memory storage, restart) | no |
| Teacher PIN sign-in, bearer tokens, role separation | yes | yes | native + E2E + JVM | no |
| Roster import/upsert, section targeting, 50-student limit | yes | yes | native + E2E + JVM | no |
| Activities: create draft, publish, complete | yes | yes | native + E2E | no |
| Examinations: builder, MC/text questions, grading, pass mark | yes | yes | native + E2E + JVM | no |
| Sessions: code, start/pause/resume/end/expiry, membership | yes | yes | native + E2E | no |
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
| Forest Tech visual language | yes | yes | not visually reviewed on device | no |

## 3. GitScribe MCP

**Not activated, not used.** Evidence gathered before and during implementation:

- no `gitscribe-mcp` tool is present in this session's tool list;
- `gitscribe-mcp` is not on `PATH`;
- no `.mcp.json` or `opencode.jsonc` exists in the project;
- it is not among installed npm globals.

`AGENTS.md` asks for GitScribe before changes; the task prompt asks to report the blocker and
continue. Documentation was therefore written directly into `docs/`. If you provide the MCP
command/path or permission to configure it, the work log and these documents can be pushed through
GitScribe; until then no GitScribe operation is claimed.

## 4. Known limitations

1. **Access-point capacity.** ESP32-S3 associates at most 10 stations, so one controller serves
   ~8–10 phones, not 40–50. The data model holds 50. Options are in `docs/FIRMWARE.md` §5.
2. **No real-time clock.** No absolute scheduling, no wall-clock timestamps in results. Sessions
   expire by duration; attendance dates come from the teacher's phone.
3. **No TLS.** Bearer tokens travel on a WPA2 classroom LAN. Documented, not hidden.
4. **One attempt per exam**, single teacher PIN, no multi-teacher roles.
5. **No destructive operations** (delete student/item/submission) and **no CSV export** in V1;
   the corresponding prototype buttons were removed rather than left non-functional.
6. **No QR scanning**; codes are typed. No camera permission is requested.
7. **No Wi-Fi auto-connect**; the user joins `SNTCH-CLASS` in system settings.
8. **UI unverified on a device.** No screenshot, layout or gesture claim is made.
9. **Hardware unverified.** The firmware image has never run on a board in this environment.
10. **Nonexistent scheduling UI**: the prototype's open/due window fields were dropped instead of
    faking a clock.

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
- `firmware/src/` — ESP32-S3 boot, Wi-Fi AP, WebServer adapter, LittleFS storage, diagnostics
- `firmware/test/core_tests.cpp` — 222-check native suite
- `firmware/platformio.ini`
- `tools/host_sim/main.cpp` — simulator serving the same core
- `tools/e2e_test.py`, `tools/capture_fixtures.py`, `tools/native_build.sh`, `tools/run_sim.sh`,
  `tools/android_build.sh`
- `android/` — Gradle project, Compose teacher/student app, 33 JVM tests
- `docs/` — architecture, protocol, firmware, Android, testing, status
- `README.md`, `.gitignore`

Unchanged: `sntch-visual-design.html` (visual reference) and `Freebuff Creation/` (left untouched
per instruction). `AGENTS.md` untouched.
