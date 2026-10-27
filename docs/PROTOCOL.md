# SNTCH Local Protocol (V1)

The SNTCH controller is the single source of truth for a classroom session. Android clients
talk to it over the controller's own Wi-Fi access point. No internet, no cloud, no Raspberry Pi.

- Transport: **HTTP/1.1 over the controller access point**
- Controller address: `http://192.168.4.1` (port 80)
- Payload: **JSON (UTF-8)**, request and response bodies
- Size limit: request body **16 KiB** (`413 PAYLOAD_TOO_LARGE` above that)
- Rate limit: **120 requests/minute per client IP** (`429 RATE_LIMITED`)
- Capacity: **50 students**, 8 sessions, 16 activities, 8 examinations (20 questions each)

HTTP was chosen over raw sockets/WebSocket because it is fully supported by both the
Arduino `WebServer` component and Android's `HttpURLConnection`, survives client
reconnection trivially, and keeps the firmware RAM footprint small. Polling is deliberate:
each client polls its own state endpoint every 2–3 s while on screen.

## 1. Envelopes

Every response is one of these two shapes.

Success:

```json
{ "ok": true, "...": "endpoint-specific fields" }
```

Failure:

```json
{ "ok": false, "error": { "code": "SESSION_ENDED", "message": "This session has ended." } }
```

| code | HTTP | meaning |
|---|---|---|
| `BAD_REQUEST` | 400 | malformed JSON, missing/invalid field, bad value |
| `UNAUTHORIZED` | 401 | missing/expired/invalid token or PIN |
| `FORBIDDEN` | 403 | token valid but not allowed (wrong role, wrong section, not session member) |
| `NOT_FOUND` | 404 | unknown path, session code, item, or submission |
| `CONFLICT` | 409 | action not valid in current state (already joined, session not live) |
| `LOCKED` | 423 | examination already submitted / attempt finalized |
| `PAYLOAD_TOO_LARGE` | 413 | body over 16 KiB |
| `RATE_LIMITED` | 429 | per-IP limit exceeded |
| `INTERNAL` | 500 | storage failure or unexpected condition |

Clients must treat an unknown `code` as a generic failure and show `message`.

## 2. Authentication

Two independent bearer tokens. Both are opaque 128-bit hex strings with an expiry.

```
Authorization: Bearer <token>
```

### Teacher

`POST /api/teacher/login` with `{"pin":"123456"}` returns a teacher token (8 h). The PIN is
compiled in via `SNTCH_TEACHER_PIN` (default `246810`) and is not exposed by any endpoint.
Teacher tokens live in RAM only: after a controller restart the teacher signs in again (their
data is persisted, their token is not). Wrong PIN attempts are limited to 5 per minute per IP.

### Student

`POST /api/student/join` with the session code and student number returns a student token bound
to exactly one `(session, student)` pair (2 h, and never beyond `session end + 10 min`). The
token is the only thing that identifies "me" on student endpoints; a student can never address
another student's record because the endpoints carry no student id parameter.

## 3. Endpoints

### Public

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/api/health` | – | `fw`, `uptimeMs`, `students`, `activeSessions`, `freeHeap`, `storageUsed` |
| GET | `/api/session/{code}` | – | session preview: `code`, `title`, `refType`, `target`, `state`, `expiresInSec` |

Session preview is what powers "check code" before joining. It never leaks the roster or
question content.

### Teacher (Bearer teacher token)

| Method | Path | Body | Notes |
|---|---|---|---|
| POST | `/api/teacher/login` | `{"pin":"..."}` | public; issues token |
| GET | `/api/teacher/state` | – | full snapshot: students, activities, exams (with questions and answer keys), sessions, attendance, submissions, events, device |
| POST | `/api/teacher/students` | `{"students":[{"number":"S-001","name":"Ana Cruz","section":"9-A"}]}` | upsert by `number`; returns `added`,`updated` |
| POST | `/api/teacher/activities` | `{"title","description","instructions","target"}` | creates a **draft** |
| POST | `/api/teacher/activities/{id}/publish` | – | draft → active |
| POST | `/api/teacher/activities/{id}/complete` | – | → completed |
| POST | `/api/teacher/exams` | `{"title","instructions","target","durationMin","passingPct","questions":[{"type":"mc","prompt":"…","options":["A","B"],"correct":0} \| {"type":"text","prompt":"…","keys":["answer"]}]}` | creates a **draft**; validates question shapes |
| POST | `/api/teacher/exams/{id}/publish` | – | draft → active |
| POST | `/api/teacher/sessions` | `{"refType":"activity"\|"exam","refId":1,"expiresInMin":30}` | only for published items of the same type; refuses a second open session for the same item |
| POST | `/api/teacher/sessions/{id}/start` | – | `ready` → `live` |
| POST | `/api/teacher/sessions/{id}/pause` | – | `live` → `paused` (freezes exam clocks) |
| POST | `/api/teacher/sessions/{id}/resume` | – | `paused` → `live` |
| POST | `/api/teacher/sessions/{id}/end` | – | → `ended`; finalizes in-progress attempts as timed-out submissions |
| POST | `/api/teacher/attendance` | `{"date":"2026-10-25","section":"9-A","studentId":7,"status":"present"}` or `{"date":"…","section":"9-A","markAll":"present"}` | status ∈ `present` \| `late` \| `absent` \| `unmarked` |
| GET | `/api/teacher/submissions/{id}` | – | full submission including stored answers and the examination's prompts (without answer keys) |
| POST | `/api/teacher/submissions/{id}/review` | – | activity submission → `reviewed` |

`GET /api/teacher/state` is the only snapshot endpoint; the teacher app polls it. It is
deliberately compact: examination questions are included (the teacher authored them), but
student answer text lives in `/sub/` files and is only fetched on demand.

### Student (Bearer student token)

| Method | Path | Body | Notes |
|---|---|---|---|
| POST | `/api/student/join` | `{"code":"SNT-4F7Q","studentNumber":"S-001"}` | public; idempotent while the session is open |
| GET | `/api/student/state` | – | `session`, `availableWork`, `mySubmissions`, `activeAttempt`, `resultsPolicy` |
| POST | `/api/student/leave` | – | removes membership, drops token |
| POST | `/api/student/exams/{examId}/start` | – | requires joined + session `live` + matching exam; returns questions **without** answer keys and `remainingSec` |
| POST | `/api/student/attempts/{examId}/answers` | `{"answers":{"12":"B-ish","13":1}}` | autosave; validated against the exam's question ids |
| POST | `/api/student/submit` | `{"refType":"exam"\|"activity","refId":3,"answers":{…}} \| {"refType":"activity","refId":3,"text":"…"}` | grades exams server-side; `clientKey` optional for retry de-duplication |
| GET | `/api/student/submissions/{id}` | – | own submission + answers + the examination's prompts; includes answer keys only when the teacher's `showCorrect` policy allows review |

## 4. Domain rules enforced on the controller

1. **Membership.** Student endpoints work only while the token's session is `ready`, `live`,
   or `paused`, and only while the student is still a member. A student may hold one session at a
   time: joining another open session moves the membership (and invalidates the old token), unless
   an examination is in progress there (`409 EXAM_IN_PROGRESS`). This keeps recovery sane after a
   restart, when membership is persisted but the RAM-only token is gone.
2. **Live gating.** `start`, `answers`, and `submit` require `state == live`. Submitting to a
   `paused` session returns `409 CONFLICT` with `SESSION_NOT_LIVE`; the client keeps the work
   locally and retries after resume. `end` finalizes attempts.
3. **Section targeting.** A student may only join a session whose `target` equals their
   `section`, and may only submit work for items targeting that section.
4. **One submission per student per item.** Retries return the original submission with
   `"duplicate": true` and HTTP 200 — no double counting, no lost work.
5. **Exam timing is server-side.** `remainingSec` decreases only while the session is `live`.
   At 0 the controller grades the autosaved answers and marks the submission `timeout:true`.
   Pause freezes the clock; server time is monotonic uptime, never wall clock.
6. **Answer validation.** Unknown question ids, wrong value types, or text > 200 chars are
   rejected with `BAD_REQUEST`. Unanswered questions are graded as wrong, never silently
   dropped.
7. **Teacher authority.** Pause/resume/end/review/attendance are teacher-token only.
   Students have no endpoint that can read another student's data.
8. **Session codes** are `SNT-` + 4 chars from `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (no
   ambiguous 0/O/1/I/L), unique among open sessions.

## 5. Idempotency and reconnection

- Joining twice with the same code + student number returns the same session membership and a
  fresh token; `joined` is a set, so the teacher's roster count is stable.
- Submitting twice returns the first submission. `clientKey` is accepted and echoed but the
  `(student, refType, refId)` uniqueness is the real guard.
- The Android student app keeps unsent activity text and exam answers in app storage, retries
  while the session is live, and re-joins only when the controller issues `401`.
- A client that disappears does not block anything: teacher snapshots show `joined` /
  `submitted` flags; no server-side timeouts are required for correctness.
- Re-joining the same session after a controller restart works without teacher intervention. To
  move to a different session, the student joins its code; if the app lost its token, the
  membership is moved server-side. An in-progress exam blocks the move.

## 6. Persistence and recovery

| File | Owner | Contents |
|---|---|---|
| `/state.json` | controller | students, activities, exams, sessions, attendance, submission index, id counters |
| `/sub/<id>.json` | controller | one submission with its answers/text |
| `/att/<examId>-<studentId>.json` | controller | autosaved in-progress exam answers |

Writes are staged to `*.tmp`, flushed, then renamed, so a power cut leaves either the old or
the new file (never a torn one). On boot the controller:

1. loads `/state.json`; a missing file is initialized only when the explicitly uploaded pending
   filesystem marker is present. An empty, malformed, unsupported-version, or incomplete state file
   stops startup and is left untouched. Existing valid markerless state is adopted without changing
   its data; a missing state file with an installed marker stops startup,
2. restores sessions, and **downgrades any `live` session to `paused`** (nobody can walk out of
   the room with a session still accepting work after a brownout),
3. restores attempts with their persisted `remainingSec`,
4. keeps every submission already on flash, so re-submitting after a restart returns the
   original grade.

Full storage: a failed write returns `INTERNAL` with `STORAGE_FULL`; the controller refuses new
sessions rather than silently accepting unpersisted work. Diagnostics in `/api/health`
(`storageUsed`, `freeHeap`) let the teacher see pressure before it bites.

## 7. Deliberate V1 limitations

- **No wall clock.** The ESP32 controller has no battery-backed real-time clock or internet. Activities/exams carry no absolute
  open/due timestamps and sessions expire by duration, not clock time. Attendance dates are
  supplied by the teacher app (its own clock) as `YYYY-MM-DD` and stored as plain strings.
- **No TLS.** All traffic stays on the controller's WPA2 access point. Teacher/student tokens are
  bearer strings on a network the teacher owns; this is documented as a limited-trust model, not
  a hardened one.
- **Polling, not push.** Snapshots are small and bounded; a 50-student room costs roughly
  25 requests/second worst case, within the supported ESP32 targets' capacity.
- **One attempt per exam.** The prototype's "2 attempts (planned)" stays unimplemented.
- **Results policy** is global (`resultsPolicy.showCorrect`); the teacher app toggles it locally
  because it only affects what the student screen renders.
