// SNTCH classroom domain core. Hardware free: all time is supplied by the
// caller as monotonic milliseconds, all storage goes through Storage.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sntch/json.hpp"
#include "sntch/storage.hpp"

namespace sntch {

typedef unsigned long long Ms;

namespace limits {
const size_t kStudents = 50;
const size_t kActivities = 16;
const size_t kExams = 8;
const size_t kQuestions = 20;
const size_t kOptions = 6;
const size_t kKeys = 4;
const size_t kSessions = 12;
const size_t kOpenSessions = 4;
const size_t kSubmissions = 400;
const size_t kAttendanceDays = 14;
const size_t kEvents = 30;
const size_t kTokens = 24;
const size_t kAttempts = 50;
}  // namespace limits

struct Result {
  bool ok;
  int status;
  std::string code;
  std::string message;

  Result() : ok(true), status(200) {}
  static Result good() { return Result(); }
  static Result fail(int status_, const char* code_, const char* message_) {
    Result r;
    r.ok = false;
    r.status = status_;
    r.code = code_ ? code_ : "BAD_REQUEST";
    r.message = message_ ? message_ : "Request failed.";
    return r;
  }
  // Optional machine-readable hint, e.g. UNANSWERED_QUESTIONS.
  std::string hint;
};

struct DeviceInfo {
  std::string name;
  std::string ssid;
  std::string ip;
  std::string firmware;
  size_t freeHeap;
  size_t storageUsed;
  size_t storageTotal;
};

struct Student {
  unsigned id;
  std::string number;
  std::string name;
  std::string section;
};

struct Question {
  unsigned id;
  std::string type;  // "mc" | "text"
  std::string prompt;
  std::vector<std::string> options;
  int correct;
  std::vector<std::string> keys;

  Question() : id(0), correct(0) {}
};

struct Item {
  unsigned id;
  std::string kind;  // "activity" | "exam"
  std::string title;
  std::string description;
  std::string instructions;
  std::string target;
  std::string status;  // draft | active | completed
  int durationMin;
  int passingPct;
  std::vector<Question> questions;

  Item() : id(0), durationMin(0), passingPct(60) {}
};

struct Session {
  unsigned id;
  std::string code;
  std::string refType;
  unsigned refId;
  std::string title;
  std::string target;
  std::string state;  // ready | live | paused | ended | expired
  unsigned expiresInMin;
  Ms createdMs;
  Ms expiresAtMs;
  Ms startedMs;
  Ms endedMs;
  std::vector<unsigned> joined;

  Session()
      : id(0), refId(0), expiresInMin(30), createdMs(0), expiresAtMs(0), startedMs(0), endedMs(0) {}
};

struct Submission {
  unsigned id;
  std::string refType;
  unsigned refId;
  unsigned sessionId;
  unsigned studentId;
  int score;
  int total;
  int percent;
  bool passed;
  bool timeout;
  std::string status;  // graded | pending | reviewed
  Ms submittedMs;

  Submission()
      : id(0), refId(0), sessionId(0), studentId(0), score(0), total(0), percent(0),
        passed(false), timeout(false), submittedMs(0) {}
};

struct AttendanceDay {
  std::string date;     // YYYY-MM-DD, supplied by the teacher app
  std::string section;  // e.g. 9-A
  std::vector<std::pair<unsigned, std::string> > marks;  // studentId -> present|late|absent
};

struct Attempt {
  unsigned examId;
  unsigned sessionId;
  unsigned studentId;
  Ms remainingMs;
  Ms lastMs;

  Attempt() : examId(0), sessionId(0), studentId(0), remainingMs(0), lastMs(0) {}
};

struct TeacherToken {
  std::string token;
  Ms expiresMs;
};

struct StudentToken {
  std::string token;
  unsigned studentId;
  unsigned sessionId;
  Ms expiresMs;
};

class Classroom {
 public:
  Classroom(Storage* storage, const std::string& teacherPin);

  // ---- lifecycle -----------------------------------------------------------
  bool load();
  bool save();
  bool storageError() const { return storageError_; }
  void tick(Ms now);
  void setSeed(unsigned long seed);

  // ---- teacher -------------------------------------------------------------
  Result login(const std::string& pin, Ms now, std::string& token, Ms& expiresMs);
  Result authorizeTeacher(const std::string& token, Ms now) const;
  Result addStudents(const Json& body, int& added, int& updated);
  Result createActivity(const Json& body, unsigned& id);
  Result createExam(const Json& body, unsigned& id);
  Result publishItem(const std::string& kind, unsigned id);
  Result completeItem(const std::string& kind, unsigned id, Ms now);
  Result createSession(const Json& body, Ms now, unsigned& id);
  Result sessionAction(unsigned id, const std::string& action, Ms now);
  Result markAttendance(const Json& body);
  Result reviewSubmission(unsigned id);
  Result setSettings(const Json& body);
  Result submissionDetail(unsigned id, Json& out) const;
  Json teacherSnapshot(Ms now, const DeviceInfo& info) const;

  // ---- student -------------------------------------------------------------
  Result join(const std::string& code, const std::string& number, Ms now, unsigned& studentId,
              unsigned& sessionId, std::string& token);
  Result sessionPreview(const std::string& code, Ms now, Json& out) const;
  Result authorizeStudent(const std::string& token, Ms now, unsigned& studentId,
                          unsigned& sessionId) const;
  Result studentSnapshot(unsigned studentId, Ms now, Json& out);
  Result startExam(unsigned studentId, unsigned examId, Ms now, Json& out);
  Result saveAnswers(unsigned studentId, unsigned examId, const Json& answers, Ms now);
  Result submit(unsigned studentId, const Json& body, Ms now, Json& out);
  Result leave(unsigned studentId, Ms now);
  Result ownSubmission(unsigned studentId, unsigned id, Json& out) const;

  // ---- diagnostics / tests -------------------------------------------------
  size_t studentCount() const { return students_.size(); }
  size_t submissionCount() const { return submissions_.size(); }
  size_t attemptCount() const { return attempts_.size(); }
  size_t openSessionCount() const;
  const std::vector<Student>& students() const { return students_; }
  const std::vector<Item>& activities() const { return activities_; }
  const std::vector<Item>& exams() const { return exams_; }
  const std::vector<Session>& sessions() const { return sessions_; }
  const std::vector<Submission>& submissions() const { return submissions_; }
  const std::string& lastError() const { return lastError_; }

 private:
  Storage* storage_;
  std::string teacherPin_;
  bool storageError_;
  std::string lastError_;

  std::vector<Student> students_;
  std::vector<Item> activities_;
  std::vector<Item> exams_;
  std::vector<Session> sessions_;
  std::vector<Submission> submissions_;
  std::vector<AttendanceDay> attendance_;
  std::vector<Attempt> attempts_;
  std::vector<TeacherToken> teacherTokens_;
  std::vector<StudentToken> studentTokens_;
  std::vector<Json> events_;  // {atMs,tone,text}
  bool showCorrect_;
  unsigned long rng_;

  unsigned nextStudentId_;
  unsigned nextItemId_;
  unsigned nextQuestionId_;
  unsigned nextSessionId_;
  unsigned nextSubmissionId_;

  unsigned rand32();
  std::string newToken();
  std::string newSessionCode();
  void logEvent(Ms now, const char* tone, const std::string& text);
  void prune(Ms now);

  Student* findStudent(unsigned id);
  const Student* findStudent(unsigned id) const;
  Student* findStudentByNumber(const std::string& number);
  Item* findItem(const std::string& kind, unsigned id);
  const Item* findItem(const std::string& kind, unsigned id) const;
  Session* findSession(unsigned id);
  const Session* findSession(unsigned id) const;
  Session* findSessionByCode(const std::string& code);
  Submission* findSubmission(unsigned id);
  const Submission* findSubmission(unsigned id) const;
  Submission* findSubmissionFor(unsigned studentId, const std::string& refType, unsigned refId);
  Attempt* findAttempt(unsigned studentId, unsigned examId);
  StudentToken* findStudentToken(const std::string& token);

  std::string effectiveState(const Session& s, Ms now) const;
  bool sessionAcceptsWork(const Session& s, Ms now) const;
  Result sessionWorkGate(unsigned studentId, const std::string& refType, unsigned refId, Ms now,
                         Session** out);
  Result validateAnswers(const Item& exam, const Json& answers, bool requireAll, Json& normalized) const;
  Result gradeAndStore(const Item& exam, const Json& answers, const Session& session,
                       unsigned studentId, bool timeout, Ms now, Json& out);
  void finalizeAttemptsForSession(unsigned sessionId, Ms now, const char* reason);
  void finalizeAttempt(Attempt& attempt, Ms now, const char* reason);
  bool answersPath(unsigned examId, unsigned studentId, std::string& path) const;
  bool submissionPath(unsigned id, std::string& path) const;
  Json itemJson(const Item& item, bool includeKeys) const;
  Json sessionJson(const Session& s, Ms now) const;
  Json submissionJson(const Submission& s) const;
  void clearStudentTokens(unsigned studentId);

  static bool validText(const std::string& s, size_t min, size_t max);
  static std::string trimLower(const std::string& s);
  static bool validSection(const std::string& s);
};

}  // namespace sntch
