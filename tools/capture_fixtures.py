#!/usr/bin/env python3
"""Captures real controller responses into Android test fixtures.

The fixtures are produced by tools/host_sim (which runs the firmware's core) and
then parsed by the Android models in WireFixtureTest, so a drift between the C++
and Kotlin sides of the protocol fails a test.

Usage: python tools/capture_fixtures.py   (then re-run the Android tests)
"""
import json
import os
import shutil
import subprocess
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, ".build", "host_sim.exe")
DATA = os.path.join(ROOT, ".build", "fixture-data")
OUT = os.path.join(ROOT, "android", "app", "src", "test", "resources", "controller")
PORT = 8100
BASE = f"http://127.0.0.1:{PORT}"


def req(method, path, body=None, token=None):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(BASE + path, data=data, method=method)
    request.add_header("Content-Type", "application/json")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, json.loads(response.read().decode())
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode())


def main():
    if not os.path.exists(EXE):
        raise SystemExit("build host_sim first: bash tools/native_build.sh")
    shutil.rmtree(DATA, ignore_errors=True)
    os.makedirs(OUT, exist_ok=True)
    server = subprocess.Popen([EXE, str(PORT), DATA], cwd=ROOT,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                if req("GET", "/api/health")[0] == 200:
                    break
            except Exception:
                time.sleep(0.1)
        else:
            raise SystemExit("simulator did not start")

        _, login = req("POST", "/api/teacher/login", {"pin": "246810"})
        teacher = login["token"]
        req("POST", "/api/teacher/students", {"students": [
            {"number": "001", "name": "Ana Cruz", "section": "9-A"},
            {"number": "002", "name": "Ben Lim", "section": "9-A"},
        ]}, teacher)
        _, exam = req("POST", "/api/teacher/exams", {
            "title": "Photosynthesis check",
            "instructions": "Answer every question.",
            "target": "9-A",
            "durationMin": 10,
            "passingPct": 60,
            "questions": [
                {"type": "mc", "prompt": "What do chloroplasts capture?",
                 "options": ["Light", "Sound"], "correct": 0},
                {"type": "text", "prompt": "Which gas is released?", "keys": ["Oxygen", "O2"]},
            ],
        }, teacher)
        exam_id = exam["id"]
        req("POST", f"/api/teacher/exams/{exam_id}/publish", None, teacher)
        _, session = req("POST", "/api/teacher/sessions",
                         {"refType": "exam", "refId": exam_id, "expiresInMin": 30}, teacher)
        req("POST", f"/api/teacher/sessions/{session['id']}/start", None, teacher)
        _, before_join = req("GET", "/api/teacher/state", None, teacher)
        code = before_join["sessions"][0]["code"]

        _, joined = req("POST", "/api/student/join", {"code": code, "studentNumber": "001"})
        student = joined["studentToken"]
        _, attempt = req("POST", f"/api/student/exams/{exam_id}/start", None, student)
        q1, q2 = attempt["attempt"]["questions"][0]["id"], attempt["attempt"]["questions"][1]["id"]
        req("POST", f"/api/student/attempts/{exam_id}/answers",
            {"answers": {str(q1): 0, str(q2): "oxygen"}}, student)
        _, submitted = req("POST", "/api/student/submit",
                           {"refType": "exam", "refId": exam_id}, student)
        submission_id = submitted["submission"]["id"]
        _, student_state = req("GET", "/api/student/state", None, student)
        _, teacher_state = req("GET", "/api/teacher/state", None, teacher)
        _, teacher_detail = req("GET", f"/api/teacher/submissions/{submission_id}", None, teacher)
        _, student_detail = req("GET", f"/api/student/submissions/{submission_id}", None, student)

        fixtures = {
            "teacher_state.json": teacher_state,
            "student_state.json": student_state,
            "exam_start.json": attempt,
            "teacher_submission.json": teacher_detail,
            "student_submission.json": student_detail,
            "health.json": req("GET", "/api/health")[1],
        }
        for name, payload in fixtures.items():
            with open(os.path.join(OUT, name), "w", encoding="utf-8") as handle:
                json.dump(payload, handle, indent=2, sort_keys=True)
            print("wrote", name)
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    main()
