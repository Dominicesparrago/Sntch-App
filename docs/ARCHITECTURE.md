# SNTCH Architecture (V1)

## 1. Shape of the system

```
            ┌────────────────────────────┐
            │   ESP32 (S3 or WROOM-32D) │
            │  SNTCH-EXAM access point   │
            │  192.168.4.1 : 80          │
            │                            │
            │  sntch_core                │
            │   ├ classroom domain       │
            │   ├ validation + auth      │
            │   ├ HTTP/JSON router       │
            │   └ storage interface      │
            │  LittleFS  ── flash        │
            └───────────┬────────────────┘
                        │ Wi-Fi (no internet)
        ┌───────────────┴───────────────┐
        │                               │
┌───────▼────────┐             ┌────────▼───────┐
│ Teacher app    │             │ Student app    │
│ Compose UI     │             │ Compose UI     │
│ polling 3 s    │             │ polling 3 s    │
└────────────────┘             └────────────────┘
```

There is exactly one source of truth: the classroom core running on the controller. The Android
apps render snapshots and send commands; they never decide grades, session state or attendance.
The host simulator (`tools/host_sim`) runs the *same* core and router, so protocol behaviour is
tested on a PC without a second implementation drifting from the firmware.

## 2. Component responsibilities

| Component | Responsibility |
|---|---|
| `firmware/lib/sntch_core` | Roster, activities, examinations, grading, sessions, attendance, tokens, idempotency, persistence format. Portable C++11, no Arduino/ESP-IDF calls. |
| `firmware/lib/sntch_core/api` | Request → auth → validation → domain call → JSON envelope. The only protocol implementation. |
| `firmware/src` (Arduino glue) | Wi-Fi AP, `WebServer` adapter, LittleFS `Storage`, monotonic 64-bit uptime, serial diagnostics, periodic checkpoints. |
| `android/.../data` | `SntchApi` (HttpURLConnection, 4 s connect / 8 s read), typed models, repositories, local drafts. |
| `android/.../ui` | Forest Tech Compose theme, teacher console, student app, role gate. Presentation only; all actions go through repositories. |
| `tools/host_sim` | Serves the core over TCP for E2E and manual Android testing. Single-threaded, not a production server. |

### Firmware module boundaries

```
src/main.ino             boot order, loop, checkpoint cadence, diagnostics
src/access_point.*       WPA2 AP, max clients, IP/hostname
src/api_server.*         HTTP <-> core adapter; no protocol logic
src/littlefs_storage.*   atomic file writes, directory bootstrap
src/uptime.h             millis() wrap-safe 64-bit clock
lib/sntch_core/json.*    bounded JSON parser/writer (depth 8, 2048 nodes)
lib/sntch_core/classroom.*  domain + persistence
lib/sntch_core/api.*     routes, auth, rate limit, envelopes
```

## 3. Key decisions and why

1. **HTTP/1.1 + JSON over the controller AP.** Both Arduino `WebServer` and Android
   `HttpURLConnection` support it without TLS or extra libraries. A raw TCP or WebSocket protocol
   would save bytes but add custom framing, reconnection logic and untestable surface. Requests
   are small and capped at 16 KiB.
2. **Polling instead of push.** Each client polls one endpoint every 3 s. At 10 associated
   stations that is ~7 requests/second worst case — within the supported ESP32 targets —
   and polling has no server-side per-client state to leak when a phone disappears.
3. **Server-side exam clock.** Remaining time is owned by the controller and decremented only
   while a session is `live`. A student force-closing the app cannot gain time. Answers are
   autosaved after each change, so a timeout or an ended session can be graded from the last
   saved answer set.
4. **Monotonic uptime, no wall clock.** The controller has no battery-backed wall clock or
   internet. Session expiry
   is duration-based; attempt clocks are stopwatches. Attendance dates come from the teacher
   app's clock as plain `YYYY-MM-DD` strings. Absolute "opens at / due at" fields from the
   prototype were deliberately dropped rather than faked.
5. **RAM index, flash bodies.** Session snapshots stay small: submissions and in-progress
   answer sets live in `/sub/` and `/att/` files, and the in-RAM record holds only the summary.
   That keeps a 50-student roster within the available ESP32 heap (the S3 build uses 14% of
   320 KB).
6. **Idempotency before gating.** Submitting twice returns the first submission with
   `"duplicate": true` and HTTP 200, even if the attempt was lost or the session has since
   ended. Retrying after a dropped response can never create a second record.
7. **Single APK, role gate.** One Android app; the role is a local preference. Teachers
   authenticate with a device PIN, students with a session code plus roster number. The device
   firmware is identical either way.
8. **Storage behind an interface.** `Storage` has a LittleFS implementation for the board and
   an in-memory/file implementation for tests, which is what makes corruption, full-disk and
   restart behaviour testable.

## 4. Data ownership

| Data | Owner | Persisted | Survives restart |
|---|---|---|---|
| Students, sections | controller | `/state.json` | yes |
| Activities, exam questions and keys | controller | `/state.json` | yes |
| Sessions, membership, expiry | controller | `/state.json` | yes; `live` → `paused` |
| Attendance marks | controller | `/state.json` | yes (last 14 date+section rows) |
| Submission summaries | controller | `/state.json` | yes |
| Submission answers / activity text | controller | `/sub/<id>.json` | yes |
| In-progress exam answers | controller | `/att/<exam>-<student>.json` | yes |
| Teacher/student tokens | controller RAM | no | no (re-auth/re-join) |
| UI drafts (unsent text, answers) | phone | SharedPreferences | yes, per app install |
| Visual design reference | repository | n/a | n/a |

## 5. Capacity and limits

The data model bounds are in `classroom.hpp`: 50 students, 16 activities, 8 exams × 20 questions,
12 sessions (4 open), 400 submissions, 14 attendance days, 24 tokens, 50 concurrent attempts.

The **real** hardware ceiling is lower: the configured ESP32 access point allows at most 10 stations
(`SNTCH_AP_MAX_CLIENTS`). A 40–50 student room therefore cannot be one controller AP. Practical
options, in order of cost:

1. Run the class in rotations of ≤8–10 connected devices.
2. Put a second controller in the room and split the roster by section.
3. For V1.5, expose the same API through an inexpensive external AP (the firmware would need a
   station-mode build) — explicitly out of scope for V1's "no router" requirement.

This limitation is reported in `/api/health` and the teacher app's device panel (open sessions,
students) but is not enforced by the data model.

## 6. Security model

- WPA2 access point with a build-time password; the network is teacher-owned, not public.
- Teacher operations require a bearer token issued by PIN sign-in (8 h, RAM only). Wrong PINS are
  rate-limited (120 req/min/IP covers brute force reasonably for a classroom).
- Student tokens are bound to one `(session, student)` pair; student endpoints take no student id,
  so one student cannot address another student's record. Section membership is enforced.
- Every request is parsed by a bounded JSON parser and validated per field; bodies over 16 KiB,
  unknown question ids and out-of-range values are rejected.
- Answer keys never appear in student snapshots or exam starts; they appear in result review only
  when the teacher's policy allows it.
- **Limitation:** no TLS. Tokens are bearer strings on a classroom LAN. Any student with the Wi-Fi
  password could sniff traffic with rooted hardware. V1 accepts this; do not reuse this protocol
  on a hostile network. Logs never print tokens, PINs or answer text.

## 7. Deviations from the visual prototype (deliberate)

| Prototype | V1 | Reason |
|---|---|---|
| Simulated device status and fake joins | real controller state | simulation removed |
| `localStorage` persistence | LittleFS + repositories | device-side durability |
| Scheduled open/due timestamps | draft → published → session start/pause/end | no RTC on the controller |
| QR scan screen | typed session code | no camera permission or QR dependency; a decorative scanner would be dishonest |
| "2 attempts (planned)" | one attempt per exam | not implemented |
| CSV export dialog | not implemented | no export endpoint; button removed rather than left decorative |
| Teacher directory / add teacher | single teacher PIN | multi-teacher permissions are a V2 concern |
| Clear-all-data / reset | not exposed | destructive without backups on a real device |
| Google Fonts (Manrope/DM Sans) | platform sans + monospace | avoiding a network font dependency; visual hierarchy preserved |

## 8. Session lifecycle

```
create ──▶ ready ──start──▶ live ──pause──▶ paused
                │             │  ◀─resume───┘
                │             └──────end────▶ ended
                └──expiry (duration)────────▶ expired
```

- Only a `live` session accepts `start exam`, `autosave` and `submit`.
- `end`/expiry finalizes in-progress attempts from autosaved answers, marking them
  `timeout: true`.
- A reboot downgrades any `live` session to `paused` so nobody can continue work that the
  controller cannot supervise.
- Membership is persisted, tokens are not. Joining a different session moves a student
  server-side (blocked while an exam is in progress), and endpoints re-check membership so an
  old token cannot act after a switch.

## 9. Verification posture

Everything the repository claims is backed by one of: a native core test, an HTTP end-to-end
check against the simulator, a JVM test of the Android client, or a successful firmware/APK
build. Nothing in the codebase depends on physical hardware to compile, and nothing here claims
physical validation. See `docs/TESTING.md` and `docs/STATUS.md`.
