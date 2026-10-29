// Native test suite for the SNTCH core. Built and run with tools/test_native.sh
// (uses the MSYS2 MinGW g++ with its DLL directory on PATH).
#include <cstdio>
#include <cmath>
#include <string>

#include "sntch/api.hpp"
#include "sntch/classroom.hpp"
#include "sntch/json.hpp"
#include "sntch/storage.hpp"
#include "network_recovery.h"

using namespace sntch;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    ++g_checks;                                                                  \
    if (!(cond)) {                                                               \
      ++g_failures;                                                              \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                            \
  } while (0)

template <typename T>
static std::string show(const T& value) {
  return std::to_string(value);
}
static std::string show(const std::string& value) { return value; }

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    ++g_checks;                                                                  \
    if (!((a) == (b))) {                                                         \
      ++g_failures;                                                              \
      std::printf("FAIL %s:%d  %s == %s  (%s vs %s)\n", __FILE__, __LINE__, #a,  \
                  #b, show(a).c_str(), show(b).c_str());                         \
    }                                                                            \
  } while (0)

static const char* kPin = "246810";
static const char* kPendingInitPath = "/sntch_pending_init.txt";
static const char* kPendingInitValue = "SNTCH-PENDING-INIT-V1";

static void prepareFreshFilesystem(MemoryStorage& storage) {
  storage.write(kPendingInitPath, kPendingInitValue);
}

static Json body(const char* json) {
  Json out;
  std::string err;
  if (!parseJson(json, out, &err)) std::printf("BAD TEST JSON: %s (%s)\n", json, err.c_str());
  return out;
}

static void addRoster(Classroom& room, int count) {
  Json list = Json::array();
  for (int k = 1; k <= count; ++k) {
    Json s = Json::object();
    char number[16];
    std::snprintf(number, sizeof(number), "S-%03d", k);
    s.set("number", Json::makeString(number));
    s.set("name", Json::makeString(std::string("Student ") + number));
    s.set("section", Json::makeString(k % 2 ? "9-A" : "9-B"));
    list.push(s);
  }
  Json payload = Json::object();
  payload.set("students", list);
  int added = 0, updated = 0;
  CHECK(room.addStudents(payload, added, updated).ok);
  CHECK_EQ(added, count);
}

static unsigned createExam(Classroom& room) {
  Json q1 = Json::object();
  q1.set("type", Json::makeString("mc"));
  q1.set("prompt", Json::makeString("What does a chloroplast capture?"));
  Json opts = Json::array();
  opts.push(Json::makeString("Light energy"));
  opts.push(Json::makeString("Sound"));
  opts.push(Json::makeString("Magnetism"));
  q1.set("options", opts);
  q1.set("correct", Json(0));

  Json q2 = Json::object();
  q2.set("type", Json::makeString("text"));
  q2.set("prompt", Json::makeString("Name the gas plants release."));
  Json keys = Json::array();
  keys.push(Json::makeString("Oxygen"));
  keys.push(Json::makeString("O2"));
  q2.set("keys", keys);

  Json questions = Json::array();
  questions.push(q1);
  questions.push(q2);

  Json exam = Json::object();
  exam.set("title", Json::makeString("Photosynthesis check"));
  exam.set("instructions", Json::makeString("Answer every question."));
  exam.set("target", Json::makeString("9-A"));
  exam.set("durationMin", Json(1));
  exam.set("passingPct", Json(60));
  exam.set("questions", questions);

  unsigned id = 0;
  CHECK(room.createExam(exam, id).ok);
  CHECK(room.publishItem("exam", id).ok);
  return id;
}

static void testJson() {
  Json doc;
  std::string err;
  CHECK(parseJson("{\"a\":[1,true,null,\"x\\u00e9\"]}", doc, &err));
  const Json* a = doc.find("a");
  CHECK(a && a->isArray());
  if (a) {
    CHECK_EQ(a->items.size(), static_cast<size_t>(4));
    CHECK_EQ(static_cast<int>(a->items[0].number), 1);
    CHECK(a->items[1].boolean);
    CHECK(a->items[2].isNull());
    CHECK_EQ(a->items[3].text, std::string("x\xc3\xa9"));
  }
  CHECK(!parseJson("{bad", doc, &err));
  CHECK(!parseJson("{\"a\":1} trailing", doc, &err));
  CHECK(parseJson("", doc, &err) == false);

  std::string deep;
  for (int k = 0; k < 12; ++k) deep += "[";
  for (int k = 0; k < 12; ++k) deep += "]";
  CHECK(!parseJson(deep, doc, &err));

  Json out = Json::object();
  out.set("quote", Json::makeString("a\"b\\c\n"));
  std::string dumped = out.dump();
  Json back;
  CHECK(parseJson(dumped, back, &err));
  CHECK_EQ(back.str("quote"), std::string("a\"b\\c\n"));
}

static void testRosterAndSessions() {
  MemoryStorage disk;
  prepareFreshFilesystem(disk);
  Classroom room(&disk, kPin);
  CHECK(room.load());

  addRoster(room, 4);
  CHECK_EQ(room.studentCount(), static_cast<size_t>(4));

  Json dup = Json::array();
  Json s = Json::object();
  s.set("number", Json::makeString("S-001"));
  s.set("name", Json::makeString("Renamed Student"));
  s.set("section", Json::makeString("9-B"));
  dup.push(s);
  Json payload = Json::object();
  payload.set("students", dup);
  int added = 0, updated = 0;
  CHECK(room.addStudents(payload, added, updated).ok);
  CHECK_EQ(added, 0);
  CHECK_EQ(updated, 1);
  CHECK_EQ(room.students()[0].name, std::string("Renamed Student"));
  // Restore section for later expectations.
  Json fix = body("{\"students\":[{\"number\":\"S-001\",\"name\":\"Student S-001\",\"section\":\"9-A\"}]}");
  CHECK(room.addStudents(fix, added, updated).ok);

  Json bad = body("{\"students\":[{\"number\":\"\",\"name\":\"X\",\"section\":\"9-A\"}]}");
  CHECK(!room.addStudents(bad, added, updated).ok);

  Json activity = body("{\"title\":\"Warm-up\",\"description\":\"Short task\",\"target\":\"9-A\"}");
  unsigned activityId = 0;
  CHECK(room.createActivity(activity, activityId).ok);

  // A draft cannot open a session.
  Json sessionBody = Json::object();
  sessionBody.set("refType", Json::makeString("activity"));
  sessionBody.set("refId", Json(static_cast<int>(activityId)));
  unsigned sessionId = 0;
  Result r = room.createSession(sessionBody, 1000, sessionId);
  CHECK(!r.ok);
  CHECK_EQ(r.code, std::string("ITEM_NOT_ACTIVE"));

  CHECK(room.publishItem("activity", activityId).ok);
  r = room.createSession(sessionBody, 1000, sessionId);
  CHECK(r.ok);
  CHECK_EQ(room.sessions().size(), static_cast<size_t>(1));
  std::string code = room.sessions()[0].code;
  CHECK_EQ(code.size(), static_cast<size_t>(8));
  CHECK_EQ(code.substr(0, 4), std::string("SNT-"));

  // Second session for the same item is refused.
  unsigned otherSession = 0;
  CHECK(!room.createSession(sessionBody, 1000, otherSession).ok);

  Json preview;
  CHECK(room.sessionPreview(code, 1000, preview).ok);
  CHECK_EQ(preview.find("session")->str("state"), std::string("ready"));

  CHECK(room.sessionAction(sessionId, "start", 2000).ok);
  CHECK(room.sessionAction(sessionId, "start", 2000).ok);  // idempotent by design

  unsigned studentId = 0, joinedSession = 0;
  std::string token;
  CHECK(room.join(code, "S-001", 3000, studentId, joinedSession, token).ok);
  CHECK_EQ(joinedSession, sessionId);
  CHECK(!token.empty());

  // Wrong section cannot join.
  std::string token2;
  unsigned sid2 = 0, sess2 = 0;
  Result wrong = room.join(code, "S-002", 3000, sid2, sess2, token2);
  CHECK(!wrong.ok);
  CHECK_EQ(wrong.code, std::string("SECTION_MISMATCH"));

  // Joining twice is idempotent and does not duplicate membership.
  std::string tokenAgain;
  CHECK(room.join(code, "S-001", 4000, studentId, joinedSession, tokenAgain).ok);
  CHECK_EQ(room.sessions()[0].joined.size(), static_cast<size_t>(1));
  CHECK(tokenAgain != token);

  // Unknown code.
  CHECK(!room.join("SNT-ZZZZ", "S-001", 4000, studentId, joinedSession, token).ok);

  // Student snapshot exposes the activity and the session.
  Json snap;
  CHECK(room.studentSnapshot(studentId, 4500, snap).ok);
  CHECK_EQ(snap.find("session")->str("state"), std::string("live"));
  CHECK_EQ(snap.find("me")->str("number"), std::string("S-001"));
  const Json* work = snap.find("availableWork");
  CHECK(work && work->items.size() == 1);

  // Activity submission needs a live session and text.
  Json submit = body("{\"refType\":\"activity\",\"refId\":0,\"text\":\"short\"}");
  Json out;
  CHECK(!room.submit(studentId, submit, 5000, out).ok);
  Json good = Json::object();
  good.set("refType", Json::makeString("activity"));
  good.set("refId", Json(static_cast<int>(activityId)));
  good.set("text", Json::makeString("Plants use light to build sugar stores."));
  CHECK(room.submit(studentId, good, 5000, out).ok);
  CHECK(room.submit(studentId, good, 6000, out).ok);
  CHECK(out.flag("duplicate", false));
  CHECK_EQ(room.submissionCount(), static_cast<size_t>(1));

  // Review flow.
  CHECK(room.reviewSubmission(1).ok);
  CHECK(room.reviewSubmission(1).ok);  // idempotent
  CHECK_EQ(room.submissions()[0].status, std::string("reviewed"));

  // Leaving drops membership.
  CHECK(room.leave(studentId, 7000).ok);
  CHECK_EQ(room.sessions()[0].joined.size(), static_cast<size_t>(0));
}

static void testSessionSwitching() {
  MemoryStorage disk;
  Classroom room(&disk, kPin);
  addRoster(room, 2);

  auto makeSession = [&room](const char* title, Ms now) {
    Json activity = Json::object();
    activity.set("title", Json::makeString(title));
    activity.set("target", Json::makeString("9-A"));
    unsigned itemId = 0;
    CHECK(room.createActivity(activity, itemId).ok);
    CHECK(room.publishItem("activity", itemId).ok);
    Json payload = Json::object();
    payload.set("refType", Json::makeString("activity"));
    payload.set("refId", Json(static_cast<int>(itemId)));
    unsigned sessionId = 0;
    CHECK(room.createSession(payload, now, sessionId).ok);
    return sessionId;
  };

  unsigned first = makeSession("Task A", 1000);
  unsigned second = makeSession("Task B", 1000);
  CHECK(room.sessionAction(first, "start", 1000).ok);
  CHECK(room.sessionAction(second, "start", 1000).ok);

  auto codeOf = [&room](unsigned id) {
    for (size_t k = 0; k < room.sessions().size(); ++k)
      if (room.sessions()[k].id == id) return room.sessions()[k].code;
    return std::string();
  };
  auto joinedCount = [&room](unsigned id) {
    for (size_t k = 0; k < room.sessions().size(); ++k)
      if (room.sessions()[k].id == id) return room.sessions()[k].joined.size();
    return static_cast<size_t>(0);
  };

  std::string codeA = codeOf(first);
  std::string codeB = codeOf(second);
  unsigned studentId = 0, sessionId = 0;
  std::string tokenA;
  CHECK(room.join(codeA, "S-001", 2000, studentId, sessionId, tokenA).ok);

  // Joining another open session moves the student instead of dead-ending.
  std::string tokenB;
  CHECK(room.join(codeB, "S-001", 3000, studentId, sessionId, tokenB).ok);
  CHECK_EQ(sessionId, second);
  CHECK_EQ(joinedCount(first), static_cast<size_t>(0));
  CHECK_EQ(joinedCount(second), static_cast<size_t>(1));

  // The old token can no longer act after the switch.
  unsigned ignored = 0;
  CHECK(!room.authorizeStudent(tokenA, 4000, ignored, ignored).ok);

  // An examination in progress pins the student to its session.
  unsigned examId = createExam(room);
  Json examSessionBody = Json::object();
  examSessionBody.set("refType", Json::makeString("exam"));
  examSessionBody.set("refId", Json(static_cast<int>(examId)));
  unsigned examSessionId = 0;
  CHECK(room.createSession(examSessionBody, 5000, examSessionId).ok);
  CHECK(room.sessionAction(examSessionId, "start", 5000).ok);
  std::string examCode = codeOf(examSessionId);
  std::string examToken;
  CHECK(room.join(examCode, "S-001", 6000, studentId, sessionId, examToken).ok);
  Json started;
  CHECK(room.startExam(studentId, examId, 6000, started).ok);

  // A third activity session cannot steal the student mid-exam.
  unsigned third = makeSession("Task C", 7000);
  CHECK(room.sessionAction(third, "start", 7000).ok);
  std::string codeC = codeOf(third);
  std::string tokenC;
  Result blocked = room.join(codeC, "S-001", 8000, studentId, sessionId, tokenC);
  CHECK(!blocked.ok);
  CHECK_EQ(blocked.code, std::string("EXAM_IN_PROGRESS"));
}

static void testAttendance() {
  MemoryStorage disk;
  Classroom room(&disk, kPin);
  addRoster(room, 4);

  CHECK(room.markAttendance(body("{\"date\":\"2026-10-25\",\"section\":\"9-A\",\"markAll\":\"present\"}")).ok);
  CHECK(room.markAttendance(body("{\"date\":\"2026-10-25\",\"section\":\"9-A\",\"studentId\":1,\"status\":\"late\"}")).ok);

  Json snap = room.teacherSnapshot(1000, DeviceInfo());
  const Json* days = snap.find("attendance");
  CHECK(days && days->items.size() == 1);
  CHECK_EQ(days->items[0].str("date"), std::string("2026-10-25"));
  CHECK_EQ(days->items[0].find("marks")->items.size(), static_cast<size_t>(2));

  CHECK(!room.markAttendance(body("{\"date\":\"25-10-2026\",\"section\":\"9-A\",\"status\":\"present\"}")).ok);
  CHECK(!room.markAttendance(body("{\"date\":\"2026-10-25\",\"section\":\"9-A\",\"studentId\":999,\"status\":\"present\"}")).ok);
  CHECK(!room.markAttendance(body("{\"date\":\"2026-10-25\",\"section\":\"9-A\",\"studentId\":2,\"status\":\"present\"}")).ok);  // S-002 is 9-B
  CHECK(room.markAttendance(body("{\"date\":\"2026-10-25\",\"section\":\"9-A\",\"studentId\":1,\"status\":\"unmarked\"}")).ok);

  Json snap2 = room.teacherSnapshot(2000, DeviceInfo());
  CHECK_EQ(snap2.find("attendance")->items[0].find("marks")->items.size(), static_cast<size_t>(1));

  // Clearing every mark removes the day.
  CHECK(room.markAttendance(body("{\"date\":\"2026-10-25\",\"section\":\"9-A\",\"markAll\":\"unmarked\"}")).ok);
  Json snap3 = room.teacherSnapshot(3000, DeviceInfo());
  CHECK_EQ(snap3.find("attendance")->items.size(), static_cast<size_t>(0));
}

static void testExamFlow() {
  MemoryStorage disk;
  Classroom room(&disk, kPin);
  addRoster(room, 2);
  unsigned examId = createExam(room);

  Json sessionBody = Json::object();
  sessionBody.set("refType", Json::makeString("exam"));
  sessionBody.set("refId", Json(static_cast<int>(examId)));
  sessionBody.set("expiresInMin", Json(30));
  unsigned sessionId = 0;
  CHECK(room.createSession(sessionBody, 1000, sessionId).ok);
  std::string code = room.sessions()[0].code;

  unsigned studentId = 0, sId = 0;
  std::string token;
  CHECK(room.join(code, "S-001", 2000, studentId, sId, token).ok);

  Json out;
  // Not started: no participation while the session is only ready.
  CHECK(!room.startExam(studentId, examId, 3000, out).ok);

  CHECK(room.sessionAction(sessionId, "start", 3000).ok);
  CHECK(room.startExam(studentId, examId, 4000, out).ok);
  const Json* attempt = out.find("attempt");
  CHECK(attempt != 0);
  if (!attempt) return;
  CHECK_EQ(attempt->integer("remainingSec", 0), 60);
  const Json* questions = attempt->find("questions");
  CHECK(questions && questions->items.size() == 2);
  // Answer keys must not leak to students.
  CHECK(questions->items[0].find("correct") == 0);
  CHECK(questions->items[1].find("keys") == 0);

  unsigned q1 = static_cast<unsigned>(questions->items[0].integer("id", 0));
  unsigned q2 = static_cast<unsigned>(questions->items[1].integer("id", 0));

  // Idempotent start keeps the same clock.
  Json again;
  CHECK(room.startExam(studentId, examId, 5000, again).ok);
  CHECK_EQ(again.find("attempt")->integer("remainingSec", 0), 60);

  // Autosave accepts valid ids and rejects unknown questions.
  Json answers = Json::object();
  answers.set(std::to_string(q1), Json(0));
  answers.set(std::to_string(q2), Json::makeString("oxygen"));
  CHECK(room.saveAnswers(studentId, examId, answers, 6000).ok);

  Json bad = Json::object();
  bad.set("999999", Json(1));
  CHECK(!room.saveAnswers(studentId, examId, bad, 6000).ok);

  // Pausing freezes the clock.
  CHECK(room.sessionAction(sessionId, "pause", 7000).ok);
  room.tick(37000);
  Json paused;
  CHECK(room.studentSnapshot(studentId, 37000, paused).ok);
  CHECK_EQ(paused.find("activeAttempt")->integer("remainingSec", -1), 60);

  // Submitting while paused is refused; work is not lost.
  Json partial = Json::object();
  partial.set("refType", Json::makeString("exam"));
  partial.set("refId", Json(static_cast<int>(examId)));
  Json partialAnswers = Json::object();
  partialAnswers.set(std::to_string(q1), Json(0));
  partial.set("answers", partialAnswers);
  Json submitOut;
  Result pausedSubmit = room.submit(studentId, partial, 38000, submitOut);
  CHECK(!pausedSubmit.ok);
  CHECK_EQ(pausedSubmit.code, std::string("SESSION_NOT_LIVE"));

  // Resume: 20 s of the clock is used, starters keep going.
  CHECK(room.sessionAction(sessionId, "resume", 40000).ok);
  room.tick(60000);
  // Incomplete submission is rejected.
  Result incomplete = room.submit(studentId, partial, 60000, submitOut);
  CHECK(!incomplete.ok);
  CHECK_EQ(incomplete.code, std::string("UNANSWERED_QUESTIONS"));

  // Full answers from the body override the autosave.
  Json full = Json::object();
  full.set("refType", Json::makeString("exam"));
  full.set("refId", Json(static_cast<int>(examId)));
  Json all = Json::object();
  all.set(std::to_string(q1), Json(0));
  all.set(std::to_string(q2), Json::makeString("  OXYGEN "));
  full.set("answers", all);
  CHECK(room.submit(studentId, full, 61000, submitOut).ok);
  const Json* submission = submitOut.find("submission");
  CHECK(submission != 0);
  if (submission) {
    CHECK_EQ(submission->integer("score", -1), 2);
    CHECK_EQ(submission->integer("percent", -1), 100);
    CHECK(submission->flag("passed", false));
    CHECK(!submission->flag("timeout", true));
  }
  CHECK_EQ(room.attemptCount(), static_cast<size_t>(0));

  // Duplicate submission after the fact.
  Json duplicateOut;
  CHECK(room.submit(studentId, full, 62000, duplicateOut).ok);
  CHECK(duplicateOut.flag("duplicate", false));
  CHECK_EQ(room.submissionCount(), static_cast<size_t>(1));

  // A student from another section cannot join this 9-A session.
  unsigned sid2 = 0, sess2 = 0;
  std::string token2;
  CHECK(!room.join(code, "S-002", 63000, sid2, sess2, token2).ok);
}

static void testTimeoutAndEnd() {
  MemoryStorage disk;
  Classroom room(&disk, kPin);
  addRoster(room, 2);
  unsigned examId = createExam(room);
  Json sessionBody = body("{\"refType\":\"exam\",\"refId\":0,\"expiresInMin\":60}");
  // Overwrite refId properly (body() above cannot know the id).
  sessionBody.set("refId", Json(static_cast<int>(examId)));
  unsigned sessionId = 0;
  CHECK(room.createSession(sessionBody, 1000, sessionId).ok);
  std::string code = room.sessions()[0].code;
  unsigned studentId = 0, sId = 0;
  std::string token;
  CHECK(room.join(code, "S-001", 2000, studentId, sId, token).ok);
  CHECK(room.sessionAction(sessionId, "start", 2000).ok);
  Json started;
  CHECK(room.startExam(studentId, examId, 3000, started).ok);
  const Json* qs = started.find("attempt")->find("questions");
  Json answers = Json::object();
  answers.set(std::to_string(qs->items[0].integer("id", 0)), Json(1));  // wrong on purpose
  CHECK(room.saveAnswers(studentId, examId, answers, 4000).ok);

  // One minute duration: run past the deadline; tick finalizes as timeout.
  room.tick(64000);
  CHECK_EQ(room.attemptCount(), static_cast<size_t>(0));
  CHECK_EQ(room.submissionCount(), static_cast<size_t>(1));
  CHECK(room.submissions()[0].timeout);
  CHECK_EQ(room.submissions()[0].score, 0);
  CHECK(!room.submissions()[0].passed);

  // A student from another section cannot join a 9-A session.
  unsigned sid2 = 0, sess2 = 0;
  std::string token2;
  CHECK(!room.join(code, "S-002", 65000, sid2, sess2, token2).ok);
}

static void testPersistenceAndRecovery() {
  MemoryStorage disk(512 * 1024);
  prepareFreshFilesystem(disk);
  unsigned examId = 0;
  {
    Classroom room(&disk, kPin);
    addRoster(room, 2);
    examId = createExam(room);
    Json sessionBody = Json::object();
    sessionBody.set("refType", Json::makeString("exam"));
    sessionBody.set("refId", Json(static_cast<int>(examId)));
    unsigned sessionId = 0;
    CHECK(room.createSession(sessionBody, 1000, sessionId).ok);
    Json codeJson;
    CHECK(room.sessionPreview(room.sessions()[0].code, 1000, codeJson).ok);
    std::string code = room.sessions()[0].code;
    unsigned studentId = 0, sId = 0;
    std::string token;
    CHECK(room.join(code, "S-001", 2000, studentId, sId, token).ok);
    CHECK(room.sessionAction(sessionId, "start", 2000).ok);
    Json started;
    CHECK(room.startExam(studentId, examId, 3000, started).ok);
    const Json* qs = started.find("attempt")->find("questions");
    Json answers = Json::object();
    answers.set(std::to_string(qs->items[0].integer("id", 0)), Json(0));
    answers.set(std::to_string(qs->items[1].integer("id", 0)), Json::makeString("O2"));
    CHECK(room.saveAnswers(studentId, examId, answers, 4000).ok);
    CHECK(room.save());
  }

  // Simulate a brownout restart: same flash contents, new Classroom object.
  Classroom restarted(&disk, kPin);
  CHECK(restarted.load());
  CHECK_EQ(restarted.studentCount(), static_cast<size_t>(2));
  CHECK_EQ(restarted.sessions().size(), static_cast<size_t>(1));
  CHECK_EQ(restarted.sessions()[0].state, std::string("paused"));  // live is downgraded
  CHECK_EQ(restarted.sessions()[0].joined.size(), static_cast<size_t>(1));

  // Teacher tokens do not survive a restart.
  CHECK(!restarted.authorizeTeacher("anything", 5000).ok);

  // The student can rejoin and finish; autosaved answers are still on flash.
  std::string code = restarted.sessions()[0].code;
  unsigned studentId = 0, sessionId = 0;
  std::string token;
  CHECK(restarted.join(code, "S-001", 6000, studentId, sessionId, token).ok);
  CHECK(restarted.sessionAction(sessionId, "resume", 7000).ok);
  Json resume;
  CHECK(restarted.startExam(studentId, examId, 8000, resume).ok);
  const Json* attempt = resume.find("attempt");
  CHECK(attempt && attempt->find("answers") && attempt->find("answers")->members.size() == 2);
  Json submitOut;
  Json submitBody = Json::object();
  submitBody.set("refType", Json::makeString("exam"));
  submitBody.set("refId", Json(static_cast<int>(examId)));
  CHECK(restarted.submit(studentId, submitBody, 9000, submitOut).ok);
  CHECK_EQ(restarted.submissionCount(), static_cast<size_t>(1));
  CHECK_EQ(submitOut.find("submission")->integer("percent", -1), 100);
}

static void testInvalidSavedStateIsPreserved() {
  MemoryStorage freshFilesystem;
  Classroom freshRoom(&freshFilesystem, kPin);
  CHECK(!freshFilesystem.exists("/state.json"));
  CHECK(!freshRoom.load());
  CHECK_EQ(freshRoom.lastError(), std::string("INITIALIZATION_REQUIRED"));
  prepareFreshFilesystem(freshFilesystem);
  Classroom explicitlyPrepared(&freshFilesystem, kPin);
  CHECK(explicitlyPrepared.load());
  CHECK(freshFilesystem.exists("/sntch_initialized.txt"));
  CHECK(freshFilesystem.exists("/state.json"));
  CHECK(!freshFilesystem.exists(kPendingInitPath));
  CHECK(!explicitlyPrepared.storageError());

  MemoryStorage unmarkedData;
  CHECK(unmarkedData.write("/sub/old-response.json", "keep"));
  Classroom unmarkedRoom(&unmarkedData, kPin);
  CHECK(!unmarkedRoom.load());
  CHECK_EQ(unmarkedRoom.lastError(), std::string("UNMARKED_FILESYSTEM_HAS_DATA"));
  CHECK_EQ(unmarkedData.read("/sub/old-response.json"), std::string("keep"));

  MemoryStorage pendingWithOtherData;
  prepareFreshFilesystem(pendingWithOtherData);
  CHECK(pendingWithOtherData.write("/archive/keep.json", "keep"));
  Classroom unsafePending(&pendingWithOtherData, kPin);
  CHECK(!unsafePending.load());
  CHECK_EQ(unsafePending.lastError(), std::string("PENDING_INITIALIZATION_HAS_OTHER_FILES"));
  CHECK(pendingWithOtherData.exists("/archive/keep.json"));

  MemoryStorage missingState;
  prepareFreshFilesystem(missingState);
  Classroom firstInstall(&missingState, kPin);
  CHECK(firstInstall.load());
  CHECK(missingState.remove("/state.json"));
  Classroom rebootedMissing(&missingState, kPin);
  CHECK(!rebootedMissing.load());
  CHECK_EQ(rebootedMissing.lastError(), std::string("INITIALIZED_STATE_MISSING"));
  CHECK(missingState.exists("/sntch_initialized.txt"));

  // Simulate power loss after state.json was staged but before its rename and
  // initialization marker commit. A complete temp snapshot is recovered.
  MemoryStorage interrupted;
  prepareFreshFilesystem(interrupted);
  MemoryStorage snapshotStorage;
  prepareFreshFilesystem(snapshotStorage);
  Classroom snapshot(&snapshotStorage, kPin);
  CHECK(snapshot.load());
  const std::string savedState = snapshotStorage.read("/state.json");
  CHECK(interrupted.write("/state.json.tmp", savedState));
  Classroom recovered(&interrupted, kPin);
  CHECK(recovered.load());
  CHECK(interrupted.exists("/state.json"));
  CHECK(interrupted.exists("/sntch_initialized.txt"));
  CHECK(!interrupted.exists(kPendingInitPath));

  // A valid pre-marker SNTCH snapshot is adopted in place without changing its data.
  MemoryStorage legacy;
  CHECK(legacy.write("/state.json", savedState));
  Classroom adoptedLegacy(&legacy, kPin);
  CHECK(adoptedLegacy.load());
  CHECK(legacy.exists("/sntch_initialized.txt"));
  CHECK_EQ(legacy.read("/state.json"), savedState);

  MemoryStorage emptyState;
  CHECK(emptyState.write("/state.json", ""));
  Classroom emptyRoom(&emptyState, kPin);
  CHECK(!emptyRoom.load());
  CHECK(emptyRoom.storageError());
  CHECK(emptyState.exists("/state.json"));
  CHECK_EQ(emptyState.read("/state.json"), std::string());

  MemoryStorage malformedState;
  CHECK(malformedState.write("/state.json", "{\"v\":1,broken"));
  Classroom malformedRoom(&malformedState, kPin);
  CHECK(!malformedRoom.load());
  CHECK_EQ(malformedState.read("/state.json"), std::string("{\"v\":1,broken"));

  MemoryStorage incompatibleState;
  const std::string original = "{\"v\":99,\"students\":[]}";
  CHECK(incompatibleState.write("/state.json", original));
  Classroom incompatibleRoom(&incompatibleState, kPin);
  CHECK(!incompatibleRoom.load());
  CHECK(incompatibleRoom.storageError());
  CHECK_EQ(incompatibleRoom.lastError(), std::string("CORRUPT_OR_INCOMPATIBLE_STATE"));
  CHECK_EQ(incompatibleState.read("/state.json"), original);

  MemoryStorage incompleteState;
  const std::string missingArray = "{\"v\":1,\"students\":[]}";
  CHECK(incompleteState.write("/state.json", missingArray));
  Classroom incompleteRoom(&incompleteState, kPin);
  CHECK(!incompleteRoom.load());
  CHECK_EQ(incompleteState.read("/state.json"), missingArray);
}

static void testStorageFull() {
  MemoryStorage disk;
  prepareFreshFilesystem(disk);
  Classroom room(&disk, kPin);
  CHECK(room.load());
  disk.setCapacity(300);
  Json list = Json::array();
  Json s = Json::object();
  s.set("number", Json::makeString("S-001"));
  s.set("name", Json::makeString("Student"));
  s.set("section", Json::makeString("9-A"));
  list.push(s);
  Json payload = Json::object();
  payload.set("students", list);
  int added = 0, updated = 0;
  Result r = room.addStudents(payload, added, updated);
  CHECK(!r.ok);
  CHECK_EQ(r.code, std::string("STORAGE_FULL"));
}

static void testApi() {
  MemoryStorage disk(512 * 1024);
  prepareFreshFilesystem(disk);
  Classroom room(&disk, kPin);
  room.load();
  DeviceInfo info;
  info.name = "SNTCH-TEST";
  info.ssid = "SNTCH-EXAM";
  info.ip = "192.168.4.1";
  info.firmware = "0.1.0-test";
  info.freeHeap = 200000;
  info.storageUsed = 1024;
  info.storageTotal = 1024 * 1024;
  Api api(&room, info);
  Ms now = 1000;

  Request health;
  health.method = "GET";
  health.path = "/api/health";
  health.nowMs = now;
  Response r = api.handle(health);
  CHECK_EQ(r.status, 200);
  CHECK_EQ(r.contentType, std::string("application/json"));
  Json healthJson;
  CHECK(parseJson(r.body, healthJson, 0));
  CHECK(healthJson.flag("ok", false));
  CHECK_EQ(healthJson.str("firmware"), std::string("0.1.0-test"));
  CHECK_EQ(healthJson.str("ssid"), std::string("SNTCH-EXAM"));
  CHECK_EQ(healthJson.str("ip"), std::string("192.168.4.1"));

  Request badLogin;
  badLogin.method = "POST";
  badLogin.path = "/api/teacher/login";
  badLogin.body = "{\"pin\":\"000000\"}";
  badLogin.nowMs = now;
  CHECK_EQ(api.handle(badLogin).status, 401);

  Request login = badLogin;
  login.body = "{\"pin\":\"246810\"}";
  r = api.handle(login);
  CHECK_EQ(r.status, 200);
  Json loginJson;
  CHECK(parseJson(r.body, loginJson, 0));
  std::string token = loginJson.str("token");
  CHECK(!token.empty());

  Request roster;
  roster.method = "POST";
  roster.path = "/api/teacher/students";
  roster.auth = "Bearer " + token;
  roster.body =
      "{\"students\":[{\"number\":\"S-001\",\"name\":\"Ana Cruz\",\"section\":\"9-A\"},"
      "{\"number\":\"S-002\",\"name\":\"Ben Lim\",\"section\":\"9-B\"}]}";
  roster.nowMs = now += 10;
  CHECK_EQ(api.handle(roster).status, 200);

  // Unauthorised state read.
  Request state;
  state.method = "GET";
  state.path = "/api/teacher/state";
  state.nowMs = now += 10;
  CHECK_EQ(api.handle(state).status, 401);
  state.auth = "Bearer " + token;
  r = api.handle(state);
  CHECK_EQ(r.status, 200);

  // Exam create + publish through the API.
  Request exam;
  exam.method = "POST";
  exam.path = "/api/teacher/exams";
  exam.auth = "Bearer " + token;
  exam.body =
      "{\"title\":\"Quiz 1\",\"target\":\"9-A\",\"durationMin\":5,\"passingPct\":50,"
      "\"questions\":[{\"type\":\"mc\",\"prompt\":\"2+2?\",\"options\":[\"3\",\"4\"],\"correct\":1}]}";
  exam.nowMs = now += 10;
  r = api.handle(exam);
  CHECK_EQ(r.status, 200);
  Json examJson;
  CHECK(parseJson(r.body, examJson, 0));
  unsigned examId = static_cast<unsigned>(examJson.integer("id", 0));
  CHECK(examId > 0);

  Request publish = exam;
  publish.path = "/api/teacher/exams/" + std::to_string(examId) + "/publish";
  publish.body = "";
  publish.nowMs = now += 10;
  CHECK_EQ(api.handle(publish).status, 200);

  Request session = exam;
  session.path = "/api/teacher/sessions";
  session.body = "{\"refType\":\"exam\",\"refId\":" + std::to_string(examId) + ",\"expiresInMin\":30}";
  session.nowMs = now += 10;
  r = api.handle(session);
  CHECK_EQ(r.status, 200);
  Json sessionJson;
  CHECK(parseJson(r.body, sessionJson, 0));
  unsigned sessionId = static_cast<unsigned>(sessionJson.integer("id", 0));

  Request start = session;
  start.path = "/api/teacher/sessions/" + std::to_string(sessionId) + "/start";
  start.body = "";
  start.nowMs = now += 10;
  CHECK_EQ(api.handle(start).status, 200);

  // Public preview exposes the code.
  Request preview;
  preview.method = "GET";
  preview.path = "/api/teacher/state";
  preview.auth = "Bearer " + token;
  preview.nowMs = now += 10;
  r = api.handle(preview);
  Json teacherState;
  CHECK(parseJson(r.body, teacherState, 0));
  std::string code = teacherState.find("sessions")->items[0].str("code");

  Request join;
  join.method = "POST";
  join.path = "/api/student/join";
  join.body = "{\"code\":\"" + code + "\",\"studentNumber\":\"S-001\"}";
  join.nowMs = now += 10;
  r = api.handle(join);
  CHECK_EQ(r.status, 200);
  Json joinJson;
  CHECK(parseJson(r.body, joinJson, 0));
  std::string studentToken = joinJson.str("studentToken");

  Request ss;
  ss.method = "GET";
  ss.path = "/api/student/state";
  ss.auth = "Bearer " + studentToken;
  ss.nowMs = now += 10;
  r = api.handle(ss);
  CHECK_EQ(r.status, 200);

  Request examStart;
  examStart.method = "POST";
  examStart.path = "/api/student/exams/" + std::to_string(examId) + "/start";
  examStart.auth = "Bearer " + studentToken;
  examStart.nowMs = now += 10;
  r = api.handle(examStart);
  CHECK_EQ(r.status, 200);
  Json attemptJson;
  CHECK(parseJson(r.body, attemptJson, 0));
  unsigned qid = static_cast<unsigned>(attemptJson.find("attempt")->find("questions")->items[0].integer("id", 0));

  Request answer;
  answer.method = "POST";
  answer.path = "/api/student/attempts/" + std::to_string(examId) + "/answers";
  answer.auth = "Bearer " + studentToken;
  answer.body = "{\"answers\":{\"" + std::to_string(qid) + "\":1}}";
  answer.nowMs = now += 10;
  CHECK_EQ(api.handle(answer).status, 200);

  Request submit;
  submit.method = "POST";
  submit.path = "/api/student/submit";
  submit.auth = "Bearer " + studentToken;
  submit.body = "{\"refType\":\"exam\",\"refId\":" + std::to_string(examId) + "}";
  submit.nowMs = now += 10;
  r = api.handle(submit);
  CHECK_EQ(r.status, 200);
  Json submitJson;
  CHECK(parseJson(r.body, submitJson, 0));
  CHECK_EQ(submitJson.find("submission")->integer("percent", -1), 100);
  unsigned submissionId = static_cast<unsigned>(submitJson.find("submission")->integer("id", 0));

  // Teacher submission detail carries prompts but not answer keys (regression:
  // a dangling pointer into a temporary once crashed this endpoint).
  Request detail;
  detail.method = "GET";
  detail.path = "/api/teacher/submissions/" + std::to_string(submissionId);
  detail.auth = "Bearer " + token;
  detail.nowMs = now += 10;
  r = api.handle(detail);
  CHECK_EQ(r.status, 200);
  Json detailJson;
  CHECK(parseJson(r.body, detailJson, 0));
  CHECK(detailJson.find("answers") != 0);
  const Json* detailQuestions = detailJson.find("questions");
  CHECK(detailQuestions && detailQuestions->items.size() == 1);
  if (detailQuestions && !detailQuestions->items.empty()) {
    CHECK(detailQuestions->items[0].find("correct") == 0);
    CHECK(detailQuestions->items[0].find("keys") == 0);
    CHECK_EQ(detailQuestions->items[0].str("prompt"), std::string("2+2?"));
  }

  // Oversized body is refused before routing.
  Request big;
  big.method = "POST";
  big.path = "/api/teacher/students";
  big.auth = "Bearer " + token;
  big.body = std::string(17000, 'x');
  big.nowMs = now += 10;
  CHECK_EQ(api.handle(big).status, 413);

  // Unknown endpoint.
  Request unknown;
  unknown.method = "GET";
  unknown.path = "/api/nope";
  unknown.nowMs = now += 10;
  CHECK_EQ(api.handle(unknown).status, 404);

  // Rate limiting kicks in for one IP.
  Request flood;
  flood.method = "GET";
  flood.path = "/api/health";
  flood.ip = "192.168.4.50";
  flood.nowMs = now;
  int limited = 0;
  for (int k = 0; k < 130; ++k) {
    flood.nowMs = now + 10;
    if (api.handle(flood).status == 429) ++limited;
  }
  CHECK(limited > 0);
}

static void testCapacity() {
  MemoryStorage disk(2 * 1024 * 1024);
  prepareFreshFilesystem(disk);
  Classroom room(&disk, kPin);
  addRoster(room, 50);
  int added = 0, updated = 0;
  Json one = body("{\"students\":[{\"number\":\"S-051\",\"name\":\"Extra\",\"section\":\"9-A\"}]}");
  Result r = room.addStudents(one, added, updated);
  CHECK(!r.ok);
  CHECK_EQ(r.code, std::string("CAPACITY_REACHED"));
}

static void testNetworkRecoveryPolicy() {
  using sntch_fw::NetworkRecovery;
  const uint32_t retryMs = 5000;
  NetworkRecovery state;

  CHECK(state.nextAction(0, retryMs) == NetworkRecovery::Action::kStartAccessPoint);
  state.noteAttempt(0);
  CHECK(state.nextAction(4999, retryMs) == NetworkRecovery::Action::kNone);
  CHECK(state.nextAction(5000, retryMs) == NetworkRecovery::Action::kStartAccessPoint);

  state.noteAttempt(5000);
  state.setAccessPointReady(true);
  CHECK(state.nextAction(5001, retryMs) == NetworkRecovery::Action::kNone);
  CHECK(state.nextAction(10000, retryMs) == NetworkRecovery::Action::kStartApi);
  state.noteAttempt(10000);
  state.setApiReady(true);
  CHECK(state.ready());
  CHECK(state.nextAction(10001, retryMs) == NetworkRecovery::Action::kNone);

  state.setAccessPointReady(false);
  CHECK(!state.ready());
  CHECK(state.apiReady());
  CHECK(state.nextAction(15000, retryMs) == NetworkRecovery::Action::kStartAccessPoint);
  state.noteAttempt(15000);
  state.setAccessPointReady(true);
  CHECK(state.ready());
  CHECK(state.apiReady());  // AP recovery reuses the existing HTTP listener.
}

static void testStatusLine() {
  MemoryStorage disk;
  prepareFreshFilesystem(disk);
  Classroom room(&disk, kPin);
  CHECK(room.load());
  CHECK_EQ(room.statusLine(1000), std::string("SNT1,0,0,0,0,0,0,none,-----,0"));

  addRoster(room, 2);
  Json activity = body("{\"title\":\"Status\",\"target\":\"9-A\"}");
  unsigned activityId = 0;
  CHECK(room.createActivity(activity, activityId).ok);
  CHECK(room.publishItem("activity", activityId).ok);
  Json sessionBody = Json::object();
  sessionBody.set("refType", Json::makeString("activity"));
  sessionBody.set("refId", Json(static_cast<int>(activityId)));
  unsigned sessionId = 0;
  CHECK(room.createSession(sessionBody, 2000, sessionId).ok);
  std::string code = room.sessions()[0].code;

  std::string line = room.statusLine(2500);
  CHECK_EQ(line.substr(0, 14), std::string("SNT1,2,1,0,0,0"));
  CHECK_EQ(line.substr(line.size() - 16), std::string("ready," + code + ",0"));

  CHECK(room.sessionAction(sessionId, "start", 3000).ok);
  unsigned studentId = 0, joinedSession = 0;
  std::string token;
  CHECK(room.join(code, "S-001", 3500, studentId, joinedSession, token).ok);
  line = room.statusLine(4000);
  CHECK_EQ(line.substr(0, 14), std::string("SNT1,2,1,1,0,0"));
  CHECK_EQ(line.substr(line.size() - 15), std::string("live," + code + ",1"));
}

static void testBackupBeforeDelete() {
  MemoryStorage disk;
  prepareFreshFilesystem(disk);
  Classroom room(&disk, kPin);
  CHECK(room.load());
  addRoster(room, 2);
  Classroom* active = &room;

  auto codeFor = [&active](unsigned id) {
    for (size_t k = 0; k < active->sessions().size(); ++k)
      if (active->sessions()[k].id == id) return active->sessions()[k].code;
    return std::string();
  };
  auto stateFor = [&active](unsigned id) {
    for (size_t k = 0; k < active->sessions().size(); ++k)
      if (active->sessions()[k].id == id) return active->sessions()[k].state;
    return std::string("missing");
  };
  auto makeActivitySession = [&active](const char* title, Ms now, unsigned& itemId,
                                       unsigned& sessionId) {
    Json a = Json::object();
    a.set("title", Json::makeString(title));
    a.set("target", Json::makeString("9-A"));
    CHECK(active->createActivity(a, itemId).ok);
    CHECK(active->publishItem("activity", itemId).ok);
    Json sb = Json::object();
    sb.set("refType", Json::makeString("activity"));
    sb.set("refId", Json(static_cast<int>(itemId)));
    CHECK(active->createSession(sb, now, sessionId).ok);
  };

  unsigned item1 = 0, session1 = 0;
  makeActivitySession("Delete ready", 1000, item1, session1);
  std::string code1 = codeFor(session1);
  Json out;
  CHECK(room.requestDeletion(session1, 1100, out).ok);
  CHECK(out.flag("pending", false));
  CHECK_EQ(stateFor(session1), std::string("pending_delete"));
  CHECK_EQ(room.pendingDeletions().size(), static_cast<size_t>(1));
  CHECK_EQ(out.integer("sessionId", 0), static_cast<int>(session1));

  Json preview;
  Result blocked = room.sessionPreview(code1, 1200, preview);
  CHECK(!blocked.ok);
  CHECK_EQ(blocked.code, std::string("SESSION_PENDING_DELETE"));
  unsigned sid = 0, jsid = 0;
  std::string token;
  blocked = room.join(code1, "S-001", 1200, sid, jsid, token);
  CHECK(!blocked.ok);
  CHECK_EQ(blocked.code, std::string("SESSION_PENDING_DELETE"));
  blocked = room.sessionAction(session1, "end", 1200);
  CHECK(!blocked.ok);
  CHECK_EQ(blocked.code, std::string("SESSION_PENDING_DELETE"));

  Json backup;
  CHECK(room.deletionBackup(session1, backup).ok);
  const Json* backupSession = backup.find("session");
  const Json* backupItem = backup.find("item");
  CHECK(backupSession != nullptr);
  CHECK(backupItem != nullptr);
  if (backupSession) CHECK_EQ(backupSession->str("state"), std::string("ready"));
  if (backupItem) CHECK_EQ(backupItem->integer("id", 0), static_cast<int>(item1));
  const Json* participants = backup.find("participants");
  CHECK(participants != nullptr);
  if (participants) CHECK_EQ(participants->items.size(), static_cast<size_t>(0));
  const std::string backupPath = out.str("backupPath");
  CHECK(disk.exists(backupPath));

  Classroom restarted(&disk, kPin);
  CHECK(restarted.load());
  active = &restarted;
  CHECK_EQ(restarted.pendingDeletions().size(), static_cast<size_t>(1));
  for (size_t k = 0; k < restarted.sessions().size(); ++k) {
    if (restarted.sessions()[k].id == session1)
      CHECK_EQ(restarted.sessions()[k].state, std::string("pending_delete"));
  }
  Json restoredBackup;
  CHECK(restarted.deletionBackup(session1, restoredBackup).ok);
  Json ack;
  CHECK(restarted.acknowledgeDeletion(session1, 1300, ack).ok);
  CHECK(ack.flag("removed", false));
  CHECK_EQ(restarted.sessions().size(), static_cast<size_t>(0));
  CHECK_EQ(restarted.pendingDeletions().size(), static_cast<size_t>(0));
  CHECK(!disk.exists(backupPath));
  CHECK(restarted.acknowledgeDeletion(session1, 1400, ack).ok);
  CHECK(ack.flag("alreadyFinalized", false));

  unsigned item2 = 0, session2 = 0;
  makeActivitySession("Cancel delete", 2000, item2, session2);
  std::string code2 = codeFor(session2);
  CHECK(restarted.requestDeletion(session2, 2100, out).ok);
  Json canceled;
  CHECK(restarted.cancelDeletion(session2, 2200, canceled).ok);
  CHECK(canceled.flag("canceled", false));
  CHECK_EQ(stateFor(session2), std::string("ready"));
  CHECK_EQ(restarted.pendingDeletions().size(), static_cast<size_t>(0));
  CHECK(restarted.sessionPreview(code2, 2300, preview).ok);
  CHECK(!disk.exists(out.str("backupPath")));

  CHECK(restarted.sessionAction(session2, "start", 2400).ok);
  Result liveBlock = restarted.requestDeletion(session2, 2500, out);
  CHECK(!liveBlock.ok);
  CHECK_EQ(liveBlock.code, std::string("SESSION_ACTIVE"));
  CHECK(restarted.sessionAction(session2, "pause", 2600).ok);
  Result pausedBlock = restarted.requestDeletion(session2, 2700, out);
  CHECK(!pausedBlock.ok);
  CHECK_EQ(pausedBlock.code, std::string("SESSION_ACTIVE"));
  CHECK(restarted.sessionAction(session2, "end", 2800).ok);
  CHECK(restarted.requestDeletion(session2, 2900, out).ok);
  CHECK(restarted.acknowledgeDeletion(session2, 3000, ack).ok);

  unsigned item3 = 0, session3 = 0;
  makeActivitySession("Delete ended with participant", 3100, item3, session3);
  std::string code3 = codeFor(session3);
  CHECK(restarted.sessionAction(session3, "start", 3200).ok);
  CHECK(restarted.join(code3, "S-001", 3300, sid, jsid, token).ok);
  CHECK(restarted.sessionAction(session3, "end", 3400).ok);
  Json endedOut;
  CHECK(restarted.requestDeletion(session3, 3500, endedOut).ok);
  Json endedBackup;
  CHECK(restarted.deletionBackup(session3, endedBackup).ok);
  const Json* endedParticipants = endedBackup.find("participants");
  CHECK(endedParticipants != nullptr);
  if (endedParticipants) {
    CHECK_EQ(endedParticipants->items.size(), static_cast<size_t>(1));
    if (!endedParticipants->items.empty())
      CHECK_EQ(endedParticipants->items[0].str("number"), std::string("S-001"));
  }
  CHECK(restarted.acknowledgeDeletion(session3, 3600, ack).ok);

  unsigned item4 = 0, session4 = 0;
  makeActivitySession("Delete with submission", 3700, item4, session4);
  std::string code4 = codeFor(session4);
  CHECK(restarted.sessionAction(session4, "start", 3800).ok);
  CHECK(restarted.join(code4, "S-001", 3900, sid, jsid, token).ok);
  Json submission = Json::object();
  submission.set("refType", Json::makeString("activity"));
  submission.set("refId", Json(static_cast<int>(item4)));
  submission.set("text", Json::makeString("This submission blocks deletion."));
  Json submitted;
  CHECK(restarted.submit(sid, submission, 4000, submitted).ok);
  CHECK(restarted.sessionAction(session4, "end", 4100).ok);
  Result submissionBlock = restarted.requestDeletion(session4, 4200, out);
  CHECK(!submissionBlock.ok);
  CHECK_EQ(submissionBlock.code, std::string("SESSION_HAS_SUBMISSIONS"));
}

int main() {
  testJson();
  testRosterAndSessions();
  testSessionSwitching();
  testAttendance();
  testExamFlow();
  testTimeoutAndEnd();
  testPersistenceAndRecovery();
  testStatusLine();
  testBackupBeforeDelete();
  testInvalidSavedStateIsPreserved();
  testStorageFull();
  testApi();
  testCapacity();
  testNetworkRecoveryPolicy();
  std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  if (g_failures) std::printf("TESTS FAILED\n");
  else std::printf("ALL TESTS PASSED\n");
  return g_failures ? 1 : 0;
}
