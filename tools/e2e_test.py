#!/usr/bin/env python3
"""SNTCH end-to-end protocol test.

Starts the host simulator (the same core + API the ESP32-S3 firmware runs),
drives complete teacher/student workflows over real HTTP, restarts the process
against the same data directory, and checks recovery behaviour.

Usage:  python tools/e2e_test.py
"""
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, ".build", "host_sim.exe")
DATA = os.path.join(ROOT, ".build", "e2e-data")
PORT = 8099
BASE = f"http://127.0.0.1:{PORT}"

checks = 0
failures = 0
server = None


def check(cond, label):
    global checks, failures
    checks += 1
    if cond:
        print(f"  ok   {label}")
    else:
        failures += 1
        print(f"  FAIL {label}")


def req(method, path, body=None, token=None, raw=None):
    data = None
    if raw is not None:
        data = raw.encode("utf-8")
    elif body is not None:
        data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(BASE + path, data=data, method=method)
    request.add_header("Content-Type", "application/json")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            payload = response.read().decode("utf-8")
            return response.status, json.loads(payload) if payload else {}
    except urllib.error.HTTPError as error:
        payload = error.read().decode("utf-8")
        try:
            return error.code, json.loads(payload)
        except json.JSONDecodeError:
            return error.code, {"raw": payload}


def start_server():
    global server
    server = subprocess.Popen(
        [EXE, str(PORT), DATA],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    for _ in range(100):
        if server.poll() is not None:
            out = server.stdout.read().decode(errors="replace")
            raise SystemExit("host_sim exited early:\n" + out)
        try:
            status, _ = req("GET", "/api/health")
            if status == 200:
                return
        except Exception:
            time.sleep(0.1)
    raise SystemExit("host_sim did not become ready")


def stop_server():
    global server
    if server and server.poll() is None:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
    server = None


def main():
    if not os.path.exists(EXE):
        raise SystemExit("host_sim.exe not found; run tools/native_build.sh first")
    shutil.rmtree(DATA, ignore_errors=True)
    start_server()
    try:
        phase_a()
        stop_server()
        time.sleep(0.4)
        start_server()
        phase_b()
    finally:
        stop_server()

    print(f"\n{checks} checks, {failures} failure(s)")
    if failures:
        print("E2E FAILED")
        return 1
    print("E2E PASSED")
    return 0


def phase_a():
    print("phase A: live classroom")
    status, health = req("GET", "/api/health")
    check(status == 200 and health["ok"], "health endpoint is up")
    check(health["students"] == 0 and health["storageError"] is False, "fresh controller state")

    status, _ = req("GET", "/api/teacher/state")
    check(status == 401, "teacher state requires a token")

    status, _ = req("POST", "/api/teacher/login", {"pin": "000000"})
    check(status == 401, "wrong teacher PIN is rejected")

    status, login = req("POST", "/api/teacher/login", {"pin": "246810"})
    check(status == 200 and len(login.get("token", "")) == 32, "teacher signs in")
    teacher = login["token"]

    roster = {
        "students": [
            {"number": "S-001", "name": "Ana Cruz", "section": "9-A"},
            {"number": "S-002", "name": "Ben Lim", "section": "9-B"},
        ]
    }
    status, result = req("POST", "/api/teacher/students", roster, teacher)
    check(status == 200 and result["added"] == 2, "roster import adds two students")
    status, result = req("POST", "/api/teacher/students", roster, teacher)
    check(status == 200 and result["updated"] == 2 and result["added"] == 0, "roster import is an upsert")

    status, activity = req(
        "POST",
        "/api/teacher/activities",
        {"title": "Warm-up", "description": "Photosynthesis", "instructions": "Two sentences.", "target": "9-A"},
        teacher,
    )
    check(status == 200, "activity created as draft")
    activity_id = activity["id"]

    status, sessions = req("GET", "/api/teacher/state", None, teacher)
    draft_sessions = sessions["sessions"]
    status, _ = req(
        "POST", "/api/teacher/sessions", {"refType": "activity", "refId": activity_id, "expiresInMin": 30}, teacher
    )
    check(status == 409, "draft activity cannot open a session")

    status, _ = req("POST", f"/api/teacher/activities/{activity_id}/publish", None, teacher)
    check(status == 200, "activity published")
    status, session = req(
        "POST", "/api/teacher/sessions", {"refType": "activity", "refId": activity_id, "expiresInMin": 30}, teacher
    )
    check(status == 200, "session created")
    activity_session = session["id"]

    status, teacher_state = req("GET", "/api/teacher/state", None, teacher)
    code = teacher_state["sessions"][0]["code"]
    check(code.startswith("SNT-") and len(code) == 8, "session code format SNT-XXXX")
    check(teacher_state["sessions"][0]["state"] == "ready", "session waits in ready")

    status, preview = req("GET", f"/api/session/{code}")
    check(status == 200 and preview["session"]["title"] == "Warm-up", "public session preview by code")
    status, _ = req("GET", "/api/session/SNT-ZZZZ")
    check(status == 404, "unknown code is not found")

    status, _ = req("POST", f"/api/teacher/sessions/{activity_session}/start", None, teacher)
    check(status == 200, "teacher starts session")

    status, joined = req("POST", "/api/student/join", {"code": code, "studentNumber": "S-001"})
    check(status == 200 and joined["session"]["state"] == "live", "student joins the live session")
    student = joined["studentToken"]
    status, again = req("POST", "/api/student/join", {"code": code, "studentNumber": "S-001"})
    check(status == 200 and again["studentToken"] != student, "rejoin is idempotent and reissues a token")
    status, state = req("GET", "/api/teacher/state", None, teacher)
    check(len(state["sessions"][0]["joined"]) == 1, "membership is not duplicated on rejoin")

    status, _ = req("POST", "/api/student/join", {"code": code, "studentNumber": "S-002"})
    check(status == 403, "other section cannot join")

    status, student_state = req("GET", "/api/student/state", None, student)
    check(status == 200 and student_state["me"]["name"] == "Ana Cruz", "student snapshot identifies the student")
    check(len(student_state["availableWork"]) == 1, "student sees the open activity")

    status, _ = req(
        "POST", "/api/student/submit", {"refType": "activity", "refId": activity_id, "text": "too short"}, student
    )
    check(status == 400, "short activity response is rejected")

    response_text = "Light energy is stored as glucose during photosynthesis."
    status, submitted = req(
        "POST", "/api/student/submit", {"refType": "activity", "refId": activity_id, "text": response_text}, student
    )
    check(status == 200 and submitted["duplicate"] is False, "activity submission accepted")
    activity_submission = submitted["submission"]["id"]
    status, duplicate = req(
        "POST", "/api/student/submit", {"refType": "activity", "refId": activity_id, "text": response_text}, student
    )
    check(status == 200 and duplicate["duplicate"] is True and duplicate["submission"]["id"] == activity_submission,
          "retry does not duplicate the submission")

    status, _ = req(
        "POST",
        "/api/teacher/attendance",
        {"date": "2026-10-25", "section": "9-A", "markAll": "present"},
        teacher,
    )
    check(status == 200, "attendance marked for the section")
    status, _ = req(
        "POST",
        "/api/teacher/attendance",
        {"date": "2026-10-25", "section": "9-A", "studentId": 1, "status": "late"},
        teacher,
    )
    check(status == 200, "single attendance mark updated")
    status, state = req("GET", "/api/teacher/state", None, teacher)
    marks = state["attendance"][0]["marks"]
    check(len(marks) == 1 and marks[0]["status"] == "late", "late mark is stored")

    status, _ = req("POST", f"/api/teacher/submissions/{activity_submission}/review", None, teacher)
    check(status == 200, "activity response reviewed")
    status, detail = req("GET", f"/api/teacher/submissions/{activity_submission}", None, teacher)
    check(status == 200 and detail["text"] == response_text, "teacher reads the stored response")

    # ---- examination flow -------------------------------------------------
    exam_body = {
        "title": "Photosynthesis check",
        "instructions": "Answer every question.",
        "target": "9-A",
        "durationMin": 5,
        "passingPct": 60,
        "questions": [
            {"type": "mc", "prompt": "What do chloroplasts capture?", "options": ["Light", "Sound"], "correct": 0},
            {"type": "text", "prompt": "Which gas is released?", "keys": ["Oxygen", "O2"]},
        ],
    }
    status, exam = req("POST", "/api/teacher/exams", exam_body, teacher)
    check(status == 200, "examination created")
    exam_id = exam["id"]
    status, _ = req("POST", f"/api/teacher/exams/{exam_id}/publish", None, teacher)
    check(status == 200, "examination published")
    status, exam_session = req(
        "POST", "/api/teacher/sessions", {"refType": "exam", "refId": exam_id, "expiresInMin": 30}, teacher
    )
    check(status == 200, "examination session created")
    exam_session_id = exam_session["id"]

    status, _ = req("POST", "/api/student/leave", None, student)
    check(status == 200, "student leaves the activity session")
    status, _ = req("POST", f"/api/teacher/sessions/{exam_session_id}/start", None, teacher)
    check(status == 200, "examination session started")
    status, teacher_state = req("GET", "/api/teacher/state", None, teacher)
    exam_code = next(s["code"] for s in teacher_state["sessions"] if s["id"] == exam_session_id)

    status, joined = req("POST", "/api/student/join", {"code": exam_code, "studentNumber": "S-001"})
    check(status == 200, "student joins the examination session")
    student = joined["studentToken"]

    status, started = req("POST", f"/api/student/exams/{exam_id}/start", None, student)
    check(status == 200, "student starts the examination")
    attempt = started["attempt"]
    check(0 < attempt["remainingSec"] <= 300, "server-side clock starts")
    questions = attempt["questions"]
    check(len(questions) == 2, "exam returns its questions")
    check(all("correct" not in q and "keys" not in q for q in questions), "answer keys never reach students")
    q1, q2 = questions[0]["id"], questions[1]["id"]

    status, _ = req(
        "POST", f"/api/student/attempts/{exam_id}/answers", {"answers": {str(q1): 0, str(q2): "oxygen"}}, student
    )
    check(status == 200, "answers autosaved")
    status, _ = req("POST", f"/api/student/attempts/{exam_id}/answers", {"answers": {"999": 1}}, student)
    check(status == 400, "unknown question id rejected")

    status, _ = req("POST", f"/api/teacher/sessions/{exam_session_id}/pause", None, teacher)
    check(status == 200, "teacher pauses the session")
    status, _ = req("GET", "/api/student/state", None, student)
    status, paused_submit = req("POST", "/api/student/submit", {"refType": "exam", "refId": exam_id}, student)
    check(status == 409 and paused_submit["error"]["code"] == "SESSION_NOT_LIVE",
          "submission while paused is refused, work kept")

    status, _ = req("POST", f"/api/teacher/sessions/{exam_session_id}/resume", None, teacher)
    check(status == 200, "teacher resumes the session")
    status, graded = req("POST", "/api/student/submit", {"refType": "exam", "refId": exam_id}, student)
    check(status == 200 and graded["submission"]["percent"] == 100, "autosaved answers graded 100%")
    check(graded["submission"]["passed"] is True, "pass mark applied")
    check(graded["submission"]["timeout"] is False, "manual submission is not a timeout")
    exam_submission = graded["submission"]["id"]

    status, duplicate = req("POST", "/api/student/submit", {"refType": "exam", "refId": exam_id}, student)
    check(status == 200 and duplicate["duplicate"] is True, "exam retry returns the original grade")

    status, detail = req("GET", f"/api/teacher/submissions/{exam_submission}", None, teacher)
    check(status == 200 and detail["answers"][str(q1)] == 0, "teacher can review stored answers")
    check(
        status == 200
        and len(detail.get("questions", [])) == 2
        and all("correct" not in q and "keys" not in q for q in detail["questions"])
        and detail["questions"][0]["prompt"] == "What do chloroplasts capture?",
        "submission detail returns prompts without answer keys",
    )

    status, _ = req("POST", f"/api/student/exams/{exam_id}/start", None, student)
    check(status == 423, "submitted examination cannot be restarted")

    # ---- second section --------------------------------------------------
    status, activity_b = req(
        "POST", "/api/teacher/activities", {"title": "9-B task", "target": "9-B", "instructions": "Write."}, teacher
    )
    activity_b_id = activity_b["id"]
    req("POST", f"/api/teacher/activities/{activity_b_id}/publish", None, teacher)
    status, session_b = req(
        "POST", "/api/teacher/sessions", {"refType": "activity", "refId": activity_b_id}, teacher
    )
    req("POST", f"/api/teacher/sessions/{session_b['id']}/start", None, teacher)
    status, teacher_state = req("GET", "/api/teacher/state", None, teacher)
    code_b = next(s["code"] for s in teacher_state["sessions"] if s["id"] == session_b["id"])
    status, joined_b = req("POST", "/api/student/join", {"code": code_b, "studentNumber": "S-002"})
    check(status == 200, "9-B student joins the 9-B session")
    student_b = joined_b["studentToken"]
    status, submitted_b = req(
        "POST", "/api/student/submit", {"refType": "activity", "refId": activity_b_id, "text": "Ben's answer about plants."}, student_b
    )
    check(status == 200, "9-B submission accepted")
    submission_b = submitted_b["submission"]["id"]

    status, _ = req("GET", f"/api/student/submissions/{submission_b}", None, student)
    check(status == 403, "a student cannot read another student's submission")
    status, _ = req("GET", f"/api/student/submissions/{submission_b}", None, student_b)
    check(status == 200, "a student can read their own submission")

    # ---- validation and limits ------------------------------------------
    status, _ = req("POST", "/api/teacher/activities", raw="{bad json", token=teacher)
    check(status == 400, "malformed JSON rejected")
    status, _ = req("POST", "/api/teacher/activities", raw=json.dumps({"x": "y" * 17000}), token=teacher)
    check(status == 413, "oversized body rejected")
    status, _ = req("GET", "/api/nope")
    check(status == 404, "unknown endpoint returns 404")
    status, _ = req("GET", "/api/student/state", None, "not-a-token")
    check(status == 401, "invalid student token rejected")

    status, state = req("GET", "/api/teacher/state", None, teacher)
    check(len(state["submissions"]) == 3, "three submissions recorded")
    check(state["sessions"][0]["state"] in ("live", "paused", "ready", "ended"), "session states are reported")
    with open(os.path.join(ROOT, ".build", "e2e-state.json"), "w", encoding="utf-8") as handle:
        json.dump(
            {
                "exam_session": exam_session_id,
                "activity_session": activity_session,
                "session_b": session_b["id"],
                "exam_id": exam_id,
            },
            handle,
        )


def phase_b():
    print("\nphase B: restart recovery (same flash directory)")
    status, health = req("GET", "/api/health")
    check(status == 200 and health["students"] == 2, "roster survived the restart")
    check(health["storageError"] is False, "state file parsed after restart")

    status, _ = req("GET", "/api/teacher/state")
    check(status == 401, "teacher token did not survive (RAM-only)")

    status, login = req("POST", "/api/teacher/login", {"pin": "246810"})
    teacher = login["token"]

    status, state = req("GET", "/api/teacher/state", None, teacher)
    sessions = {s["id"]: s for s in state["sessions"]}
    with open(os.path.join(ROOT, ".build", "e2e-state.json"), encoding="utf-8") as handle:
        saved = json.load(handle)

    check(sessions[saved["exam_session"]]["state"] == "paused", "live session reloads as paused")
    check(len(state["submissions"]) == 3, "all submissions survived the restart")
    check(len(state["attendance"]) == 1, "attendance survived the restart")

    status, _ = req("POST", f"/api/teacher/sessions/{saved['exam_session']}/resume", None, teacher)
    check(status == 200, "teacher resumes recovered session")
    exam_code = sessions[saved["exam_session"]]["code"]
    status, joined = req("POST", "/api/student/join", {"code": exam_code, "studentNumber": "S-001"})
    check(status == 200, "student rejoins after the restart")
    student = joined["studentToken"]
    status, duplicate = req("POST", "/api/student/submit", {"refType": "exam", "refId": saved["exam_id"]}, student)
    check(status == 200 and duplicate["duplicate"] is True, "duplicate guard survives a restart")

    # Session end must finalize an in-progress attempt from autosaved answers.
    status, exam2 = req(
        "POST",
        "/api/teacher/exams",
        {
            "title": "Exit ticket",
            "target": "9-A",
            "durationMin": 10,
            "passingPct": 50,
            "questions": [{"type": "mc", "prompt": "2 + 2?", "options": ["3", "4"], "correct": 1}],
        },
        teacher,
    )
    exam2_id = exam2["id"]
    req("POST", f"/api/teacher/exams/{exam2_id}/publish", None, teacher)
    status, session2 = req(
        "POST", "/api/teacher/sessions", {"refType": "exam", "refId": exam2_id, "expiresInMin": 20}, teacher
    )
    req("POST", f"/api/teacher/sessions/{session2['id']}/start", None, teacher)
    status, state = req("GET", "/api/teacher/state", None, teacher)
    exam2_code = next(s["code"] for s in state["sessions"] if s["id"] == session2["id"])

    status, _ = req("POST", "/api/teacher/sessions/" + str(saved["exam_session"]) + "/end", None, teacher)
    check(status == 200, "old session ended")
    status, _ = req("POST", "/api/student/leave", None, student)
    status, joined2 = req("POST", "/api/student/join", {"code": exam2_code, "studentNumber": "S-001"})
    check(status == 200, "student joins the exit-ticket session")
    student = joined2["studentToken"]
    status, started = req("POST", f"/api/student/exams/{exam2_id}/start", None, student)
    check(status == 200, "exit ticket started")
    qid = started["attempt"]["questions"][0]["id"]
    req("POST", f"/api/student/attempts/{exam2_id}/answers", {"answers": {str(qid): 1}}, student)
    status, _ = req("POST", f"/api/teacher/sessions/{session2['id']}/end", None, teacher)
    check(status == 200, "teacher ends the session mid-exam")
    status, state = req("GET", "/api/teacher/state", None, teacher)
    check(len(state["submissions"]) == 4, "ended session auto-submitted the attempt")
    timed = [s for s in state["submissions"] if s["refId"] == exam2_id]
    check(len(timed) == 1 and timed[0]["timeout"] is True and timed[0]["percent"] == 100,
          "auto-submission is marked timeout and graded from autosave")
    status, after_end = req("POST", "/api/student/submit", {"refType": "exam", "refId": exam2_id}, student)
    check(status in (200, 409) and (status == 409 or after_end.get("duplicate") is True),
          "post-end retry is idempotent, never new work")
    check(len(req("GET", "/api/teacher/state", None, teacher)[1]["submissions"]) == 4, "no duplicate was added")


if __name__ == "__main__":
    sys.exit(main())
