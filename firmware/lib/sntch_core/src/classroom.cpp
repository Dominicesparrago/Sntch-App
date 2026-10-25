
#include "sntch/classroom.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace sntch {
namespace {

const Ms kTeacherTokenTtlMs = 8ULL * 60 * 60 * 1000;
const Ms kStudentTokenTtlMs = 2ULL * 60 * 60 * 1000;
const Ms kSessionGraceMs = 10ULL * 60 * 1000;
const size_t kMaxAnswerBytes = 2048;

std::string formatCode(unsigned v) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%u", v);
  return std::string(buf);
}

bool inSection(const Student& s, const std::string& section) { return s.section == section; }

// Session states that still accept client work.
bool isOpenState(const std::string& state) {
  return state == "ready" || state == "live" || state == "paused";
}

Json idArray(const std::vector<unsigned>& ids) {
  Json a = Json::array();
  for (size_t k = 0; k < ids.size(); ++k) a.push(Json(static_cast<int>(ids[k])));
  return a;
}

}  // namespace

Classroom::Classroom(Storage* storage, const std::string& teacherPin)
    : storage_(storage),
      teacherPin_(teacherPin),
      storageError_(false),
      showCorrect_(true),
      rng_(0x9E3779B9UL),
      nextStudentId_(1),
      nextItemId_(1),
      nextQuestionId_(1),
      nextSessionId_(1),
      nextSubmissionId_(1) {}

unsigned Classroom::rand32() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return static_cast<unsigned>(rng_);
}

void Classroom::setSeed(unsigned long seed) { rng_ = seed ? seed : 0x9E3779B9UL; }

std::string Classroom::newToken() {
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x", rand32(), rand32(), rand32(), rand32());
  return std::string(buf);
}

std::string Classroom::newSessionCode() {
  static const char* alphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
  for (int attempt = 0; attempt < 64; ++attempt) {
    std::string code = "SNT-";
    for (int k = 0; k < 4; ++k) code.push_back(alphabet[rand32() % 31]);
    if (!findSessionByCode(code)) return code;
  }
  return "SNT-" + formatCode(rand32() % 10000);
}

bool Classroom::validText(const std::string& s, size_t min, size_t max) {
  return s.size() >= min && s.size() <= max;
}

std::string Classroom::trimLower(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
  std::string out = s.substr(a, b - a);
  for (size_t k = 0; k < out.size(); ++k) {
    if (out[k] >= 'A' && out[k] <= 'Z') out[k] = static_cast<char>(out[k] - 'A' + 'a');
  }
  return out;
}

bool Classroom::validSection(const std::string& s) {
  if (s.empty() || s.size() > 12) return false;
  for (size_t k = 0; k < s.size(); ++k) {
    char c = s[k];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

// ---------------------------------------------------------------- lookups --

Student* Classroom::findStudent(unsigned id) {
  for (size_t k = 0; k < students_.size(); ++k)
    if (students_[k].id == id) return &students_[k];
  return 0;
}

const Student* Classroom::findStudent(unsigned id) const {
  for (size_t k = 0; k < students_.size(); ++k)
    if (students_[k].id == id) return &students_[k];
  return 0;
}

Student* Classroom::findStudentByNumber(const std::string& number) {
  for (size_t k = 0; k < students_.size(); ++k)
    if (students_[k].number == number) return &students_[k];
  return 0;
}

Item* Classroom::findItem(const std::string& kind, unsigned id) {
  std::vector<Item>& list = (kind == "exam") ? exams_ : activities_;
  for (size_t k = 0; k < list.size(); ++k)
    if (list[k].id == id) return &list[k];
  return 0;
}

const Item* Classroom::findItem(const std::string& kind, unsigned id) const {
  const std::vector<Item>& list = (kind == "exam") ? exams_ : activities_;
  for (size_t k = 0; k < list.size(); ++k)
    if (list[k].id == id) return &list[k];
  return 0;
}

Session* Classroom::findSession(unsigned id) {
  for (size_t k = 0; k < sessions_.size(); ++k)
    if (sessions_[k].id == id) return &sessions_[k];
  return 0;
}

const Session* Classroom::findSession(unsigned id) const {
  for (size_t k = 0; k < sessions_.size(); ++k)
    if (sessions_[k].id == id) return &sessions_[k];
  return 0;
}

Session* Classroom::findSessionByCode(const std::string& code) {
  for (size_t k = 0; k < sessions_.size(); ++k)
    if (sessions_[k].code == code) return &sessions_[k];
  return 0;
}

Submission* Classroom::findSubmission(unsigned id) {
  for (size_t k = 0; k < submissions_.size(); ++k)
    if (submissions_[k].id == id) return &submissions_[k];
  return 0;
}

const Submission* Classroom::findSubmission(unsigned id) const {
  for (size_t k = 0; k < submissions_.size(); ++k)
    if (submissions_[k].id == id) return &submissions_[k];
  return 0;
}

Submission* Classroom::findSubmissionFor(unsigned studentId, const std::string& refType,
                                        unsigned refId) {
  for (size_t k = 0; k < submissions_.size(); ++k) {
    Submission& s = submissions_[k];
    if (s.studentId == studentId && s.refType == refType && s.refId == refId) return &s;
  }
  return 0;
}

Attempt* Classroom::findAttempt(unsigned studentId, unsigned examId) {
  for (size_t k = 0; k < attempts_.size(); ++k) {
    if (attempts_[k].studentId == studentId && attempts_[k].examId == examId) return &attempts_[k];
  }
  return 0;
}

StudentToken* Classroom::findStudentToken(const std::string& token) {
  for (size_t k = 0; k < studentTokens_.size(); ++k)
    if (studentTokens_[k].token == token) return &studentTokens_[k];
  return 0;
}

void Classroom::clearStudentTokens(unsigned studentId) {
  for (size_t k = 0; k < studentTokens_.size();) {
    if (studentTokens_[k].studentId == studentId) {
      studentTokens_.erase(studentTokens_.begin() + static_cast<long>(k));
    } else {
      ++k;
    }
  }
}

std::string Classroom::effectiveState(const Session& s, Ms now) const {
  if (isOpenState(s.state) && s.expiresAtMs > 0 && now >= s.expiresAtMs) return "expired";
  return s.state;
}

bool Classroom::sessionAcceptsWork(const Session& s, Ms now) const {
  return effectiveState(s, now) == "live";
}

// -------------------------------------------------------------- event log --

void Classroom::logEvent(Ms now, const char* tone, const std::string& text) {
  Json e = Json::object();
  e.set("atMs", Json(static_cast<double>(now)));
  e.set("tone", Json::makeString(tone));
  e.set("text", Json::makeString(text));
  events_.push_back(e);
  if (events_.size() > limits::kEvents) events_.erase(events_.begin());
}

// ------------------------------------------------------------ persistence --

bool Classroom::answersPath(unsigned examId, unsigned studentId, std::string& path) const {
  path = "/att/" + formatCode(examId) + "-" + formatCode(studentId) + ".json";
  return true;
}

bool Classroom::submissionPath(unsigned id, std::string& path) const {
  path = "/sub/" + formatCode(id) + ".json";
  return true;
}

bool Classroom::save() {
  if (!storage_) return false;
  Json root = Json::object();
  root.set("v", Json(1));
  root.set("nextStudentId", Json(static_cast<int>(nextStudentId_)));
  root.set("nextItemId", Json(static_cast<int>(nextItemId_)));
  root.set("nextQuestionId", Json(static_cast<int>(nextQuestionId_)));
  root.set("nextSessionId", Json(static_cast<int>(nextSessionId_)));
  root.set("nextSubmissionId", Json(static_cast<int>(nextSubmissionId_)));
  root.set("showCorrect", Json(showCorrect_));

  Json students = Json::array();
  for (size_t k = 0; k < students_.size(); ++k) {
    Json s = Json::object();
    s.set("id", Json(static_cast<int>(students_[k].id)));
    s.set("number", Json::makeString(students_[k].number));
    s.set("name", Json::makeString(students_[k].name));
    s.set("section", Json::makeString(students_[k].section));
    students.push(s);
  }
  root.set("students", students);

  const std::vector<Item>* lists[2] = {&activities_, &exams_};
  const char* keys[2] = {"activities", "exams"};
  for (int li = 0; li < 2; ++li) {
    Json arr = Json::array();
    for (size_t k = 0; k < lists[li]->size(); ++k) arr.push(itemJson((*lists[li])[k], true));
    root.set(keys[li], arr);
  }

  Json sessions = Json::array();
  for (size_t k = 0; k < sessions_.size(); ++k) {
    const Session& s = sessions_[k];
    Json o = Json::object();
    o.set("id", Json(static_cast<int>(s.id)));
    o.set("code", Json::makeString(s.code));
    o.set("refType", Json::makeString(s.refType));
    o.set("refId", Json(static_cast<int>(s.refId)));
    o.set("title", Json::makeString(s.title));
    o.set("target", Json::makeString(s.target));
    o.set("state", Json::makeString(s.state));
    o.set("expiresInMin", Json(static_cast<int>(s.expiresInMin)));
    o.set("createdMs", Json(static_cast<double>(s.createdMs)));
    o.set("expiresAtMs", Json(static_cast<double>(s.expiresAtMs)));
    o.set("startedMs", Json(static_cast<double>(s.startedMs)));
    o.set("endedMs", Json(static_cast<double>(s.endedMs)));
    o.set("joined", idArray(s.joined));
    sessions.push(o);
  }
  root.set("sessions", sessions);

  Json subs = Json::array();
  for (size_t k = 0; k < submissions_.size(); ++k) subs.push(submissionJson(submissions_[k]));
  root.set("submissions", subs);

  Json att = Json::array();
  for (size_t k = 0; k < attendance_.size(); ++k) {
    Json day = Json::object();
    day.set("date", Json::makeString(attendance_[k].date));
    day.set("section", Json::makeString(attendance_[k].section));
    Json marks = Json::array();
    for (size_t m = 0; m < attendance_[k].marks.size(); ++m) {
      Json mark = Json::object();
      mark.set("studentId", Json(static_cast<int>(attendance_[k].marks[m].first)));
      mark.set("status", Json::makeString(attendance_[k].marks[m].second));
      marks.push(mark);
    }
    day.set("marks", marks);
    att.push(day);
  }
  root.set("attendance", att);

  Json attempts = Json::array();
  for (size_t k = 0; k < attempts_.size(); ++k) {
    Json a = Json::object();
    a.set("examId", Json(static_cast<int>(attempts_[k].examId)));
    a.set("sessionId", Json(static_cast<int>(attempts_[k].sessionId)));
    a.set("studentId", Json(static_cast<int>(attempts_[k].studentId)));
    a.set("remainingMs", Json(static_cast<double>(attempts_[k].remainingMs)));
    attempts.push(a);
  }
  root.set("attempts", attempts);

  Json events = Json::array();
  for (size_t k = 0; k < events_.size(); ++k) events.push(events_[k]);
  root.set("events", events);

  if (!storage_->writeAtomic("/state.json", root.dump())) {
    lastError_ = "STORAGE_FULL";
    return false;
  }
  lastError_.clear();
  return true;
}

bool Classroom::load() {
  storageError_ = false;
  if (!storage_) {
    storageError_ = true;
    return false;
  }
  std::string raw = storage_->read("/state.json");
  if (raw.empty()) return true;  // fresh device

  Json root;
  std::string err;
  if (!parseJson(raw, root, &err) || !root.isObject()) {
    storageError_ = true;
    lastError_ = "CORRUPT_STATE";
    return false;
  }

  nextStudentId_ = static_cast<unsigned>(root.integer("nextStudentId", 1));
  nextItemId_ = static_cast<unsigned>(root.integer("nextItemId", 1));
  nextQuestionId_ = static_cast<unsigned>(root.integer("nextQuestionId", 1));
  nextSessionId_ = static_cast<unsigned>(root.integer("nextSessionId", 1));
  nextSubmissionId_ = static_cast<unsigned>(root.integer("nextSubmissionId", 1));
  showCorrect_ = root.flag("showCorrect", true);

  students_.clear();
  const Json* students = root.find("students");
  if (students && students->isArray()) {
    for (size_t k = 0; k < students->items.size() && students_.size() < limits::kStudents; ++k) {
      const Json& s = students->items[k];
      Student st;
      st.id = static_cast<unsigned>(s.integer("id", 0));
      st.number = s.str("number");
      st.name = s.str("name");
      st.section = s.str("section");
      if (st.id && !st.name.empty()) students_.push_back(st);
    }
  }

  const char* itemKeys[2] = {"activities", "exams"};
  std::vector<Item>* lists[2] = {&activities_, &exams_};
  for (int li = 0; li < 2; ++li) {
    lists[li]->clear();
    const Json* arr = root.find(itemKeys[li]);
    if (!arr || !arr->isArray()) continue;
    size_t cap = (li == 0) ? limits::kActivities : limits::kExams;
    for (size_t k = 0; k < arr->items.size() && lists[li]->size() < cap; ++k) {
      const Json& o = arr->items[k];
      Item item;
      item.id = static_cast<unsigned>(o.integer("id", 0));
      item.kind = o.str("kind", li == 0 ? "activity" : "exam");
      item.title = o.str("title");
      item.description = o.str("description");
      item.instructions = o.str("instructions");
      item.target = o.str("target");
      item.status = o.str("status", "draft");
      item.durationMin = o.integer("durationMin", 15);
      item.passingPct = o.integer("passingPct", 60);
      const Json* qs = o.find("questions");
      if (qs && qs->isArray()) {
        for (size_t q = 0; q < qs->items.size() && item.questions.size() < limits::kQuestions; ++q) {
          const Json& qj = qs->items[q];
          Question question;
          question.id = static_cast<unsigned>(qj.integer("id", 0));
          question.type = qj.str("type", "mc");
          question.prompt = qj.str("prompt");
          question.correct = qj.integer("correct", 0);
          const Json* opts = qj.find("options");
          if (opts && opts->isArray()) {
            for (size_t x = 0; x < opts->items.size() && question.options.size() < limits::kOptions; ++x)
              if (opts->items[x].isString()) question.options.push_back(opts->items[x].text);
          }
          const Json* keys = qj.find("keys");
          if (keys && keys->isArray()) {
            for (size_t x = 0; x < keys->items.size() && question.keys.size() < limits::kKeys; ++x)
              if (keys->items[x].isString()) question.keys.push_back(keys->items[x].text);
          }
          if (question.id) item.questions.push_back(question);
        }
      }
      if (item.id && !item.title.empty()) lists[li]->push_back(item);
    }
  }

  sessions_.clear();
  const Json* sessions = root.find("sessions");
  if (sessions && sessions->isArray()) {
    for (size_t k = 0; k < sessions->items.size() && sessions_.size() < limits::kSessions; ++k) {
      const Json& o = sessions->items[k];
      Session s;
      s.id = static_cast<unsigned>(o.integer("id", 0));
      s.code = o.str("code");
      s.refType = o.str("refType", "activity");
      s.refId = static_cast<unsigned>(o.integer("refId", 0));
      s.title = o.str("title");
      s.target = o.str("target");
      s.state = o.str("state", "ended");
      s.expiresInMin = static_cast<unsigned>(o.integer("expiresInMin", 30));
      s.createdMs = static_cast<Ms>(o.num("createdMs", 0));
      s.expiresAtMs = static_cast<Ms>(o.num("expiresAtMs", 0));
      s.startedMs = static_cast<Ms>(o.num("startedMs", 0));
      s.endedMs = static_cast<Ms>(o.num("endedMs", 0));
      const Json* joined = o.find("joined");
      if (joined && joined->isArray()) {
        for (size_t j = 0; j < joined->items.size(); ++j) {
          if (joined->items[j].isNumber())
            s.joined.push_back(static_cast<unsigned>(joined->items[j].number));
        }
      }
      // Safety: a controller that restarts mid-lesson must never come back with
      // a session that silently accepts work. Downgrade live -> paused.
      if (s.state == "live") s.state = "paused";
      if (s.id && !s.code.empty()) sessions_.push_back(s);
    }
  }

  submissions_.clear();
  const Json* subs = root.find("submissions");
  if (subs && subs->isArray()) {
    for (size_t k = 0; k < subs->items.size() && submissions_.size() < limits::kSubmissions; ++k) {
      const Json& o = subs->items[k];
      Submission s;
      s.id = static_cast<unsigned>(o.integer("id", 0));
      s.refType = o.str("refType", "activity");
      s.refId = static_cast<unsigned>(o.integer("refId", 0));
      s.sessionId = static_cast<unsigned>(o.integer("sessionId", 0));
      s.studentId = static_cast<unsigned>(o.integer("studentId", 0));
      s.score = o.integer("score", 0);
      s.total = o.integer("total", 0);
      s.percent = o.integer("percent", 0);
      s.passed = o.flag("passed", false);
      s.timeout = o.flag("timeout", false);
      s.status = o.str("status", "pending");
      s.submittedMs = static_cast<Ms>(o.num("submittedMs", 0));
      if (s.id) submissions_.push_back(s);
    }
  }

  attendance_.clear();
  const Json* att = root.find("attendance");
  if (att && att->isArray()) {
    for (size_t k = 0; k < att->items.size() && attendance_.size() < limits::kAttendanceDays; ++k) {
      const Json& o = att->items[k];
      AttendanceDay day;
      day.date = o.str("date");
      day.section = o.str("section");
      const Json* marks = o.find("marks");
      if (marks && marks->isArray()) {
        for (size_t m = 0; m < marks->items.size(); ++m) {
          const Json& mark = marks->items[m];
          day.marks.push_back(std::make_pair(
              static_cast<unsigned>(mark.integer("studentId", 0)), mark.str("status")));
        }
      }
      if (!day.date.empty() && !day.section.empty()) attendance_.push_back(day);
    }
  }

  attempts_.clear();
  const Json* atts = root.find("attempts");
  if (atts && atts->isArray()) {
    for (size_t k = 0; k < atts->items.size() && attempts_.size() < limits::kAttempts; ++k) {
      const Json& o = atts->items[k];
      Attempt a;
      a.examId = static_cast<unsigned>(o.integer("examId", 0));
      a.sessionId = static_cast<unsigned>(o.integer("sessionId", 0));
      a.studentId = static_cast<unsigned>(o.integer("studentId", 0));
      a.remainingMs = static_cast<Ms>(o.num("remainingMs", 0));
      a.lastMs = 0;
      if (a.examId && a.studentId) attempts_.push_back(a);
    }
  }

  events_.clear();
  const Json* events = root.find("events");
  if (events && events->isArray()) {
    for (size_t k = 0; k < events->items.size() && events_.size() < limits::kEvents; ++k)
      events_.push_back(events->items[k]);
  }

  // Keep id counters ahead of everything that survived the restart.
  for (size_t k = 0; k < students_.size(); ++k)
    if (students_[k].id >= nextStudentId_) nextStudentId_ = students_[k].id + 1;
  for (size_t k = 0; k < activities_.size(); ++k)
    if (activities_[k].id >= nextItemId_) nextItemId_ = activities_[k].id + 1;
  for (size_t k = 0; k < exams_.size(); ++k)
    if (exams_[k].id >= nextItemId_) nextItemId_ = exams_[k].id + 1;
  for (size_t k = 0; k < sessions_.size(); ++k)
    if (sessions_[k].id >= nextSessionId_) nextSessionId_ = sessions_[k].id + 1;
  for (size_t k = 0; k < submissions_.size(); ++k)
    if (submissions_[k].id >= nextSubmissionId_) nextSubmissionId_ = submissions_[k].id + 1;

  return true;
}

// --------------------------------------------------------------- snapshots --

Json Classroom::itemJson(const Item& item, bool includeKeys) const {
  Json o = Json::object();
  o.set("id", Json(static_cast<int>(item.id)));
  o.set("kind", Json::makeString(item.kind));
  o.set("title", Json::makeString(item.title));
  o.set("description", Json::makeString(item.description));
  o.set("instructions", Json::makeString(item.instructions));
  o.set("target", Json::makeString(item.target));
  o.set("status", Json::makeString(item.status));
  o.set("durationMin", Json(item.durationMin));
  o.set("passingPct", Json(item.passingPct));
  o.set("questionCount", Json(static_cast<int>(item.questions.size())));
  Json qs = Json::array();
  for (size_t k = 0; k < item.questions.size(); ++k) {
    const Question& q = item.questions[k];
    Json qo = Json::object();
    qo.set("id", Json(static_cast<int>(q.id)));
    qo.set("type", Json::makeString(q.type));
    qo.set("prompt", Json::makeString(q.prompt));
    Json opts = Json::array();
    for (size_t x = 0; x < q.options.size(); ++x) opts.push(Json::makeString(q.options[x]));
    qo.set("options", opts);
    if (includeKeys) {
      qo.set("correct", Json(q.correct));
      Json keys = Json::array();
      for (size_t x = 0; x < q.keys.size(); ++x) keys.push(Json::makeString(q.keys[x]));
      qo.set("keys", keys);
    }
    qs.push(qo);
  }
  o.set("questions", qs);
  return o;
}

Json Classroom::sessionJson(const Session& s, Ms now) const {
  Json o = Json::object();
  o.set("id", Json(static_cast<int>(s.id)));
  o.set("code", Json::makeString(s.code));
  o.set("refType", Json::makeString(s.refType));
  o.set("refId", Json(static_cast<int>(s.refId)));
  o.set("title", Json::makeString(s.title));
  o.set("target", Json::makeString(s.target));
  o.set("state", Json::makeString(effectiveState(s, now)));
  o.set("expiresInSec", Json(static_cast<int>(s.expiresAtMs > now ? (s.expiresAtMs - now) / 1000 : 0)));
  o.set("createdMs", Json(static_cast<double>(s.createdMs)));
  o.set("startedMs", Json(static_cast<double>(s.startedMs)));
  o.set("endedMs", Json(static_cast<double>(s.endedMs)));
  o.set("joined", idArray(s.joined));
  const Item* item = findItem(s.refType, s.refId);
  o.set("itemStatus", Json::makeString(item ? item->status : "missing"));
  return o;
}

Json Classroom::submissionJson(const Submission& s) const {
  Json o = Json::object();
  o.set("id", Json(static_cast<int>(s.id)));
  o.set("refType", Json::makeString(s.refType));
  o.set("refId", Json(static_cast<int>(s.refId)));
  o.set("sessionId", Json(static_cast<int>(s.sessionId)));
  o.set("studentId", Json(static_cast<int>(s.studentId)));
  const Student* st = findStudent(s.studentId);
  o.set("studentName", Json::makeString(st ? st->name : ""));
  o.set("score", Json(s.score));
  o.set("total", Json(s.total));
  o.set("percent", Json(s.percent));
  o.set("passed", Json(s.passed));
  o.set("timeout", Json(s.timeout));
  o.set("status", Json::makeString(s.status));
  o.set("submittedMs", Json(static_cast<double>(s.submittedMs)));
  const Item* item = findItem(s.refType, s.refId);
  o.set("title", Json::makeString(item ? item->title : ""));
  return o;
}

Json Classroom::teacherSnapshot(Ms now, const DeviceInfo& info) const {
  Json root = Json::object();
  root.set("ok", Json(true));

  Json device = Json::object();
  device.set("name", Json::makeString(info.name));
  device.set("ssid", Json::makeString(info.ssid));
  device.set("ip", Json::makeString(info.ip));
  device.set("firmware", Json::makeString(info.firmware));
  device.set("uptimeMs", Json(static_cast<double>(now)));
  device.set("freeHeap", Json(static_cast<int>(info.freeHeap)));
  device.set("storageUsed", Json(static_cast<int>(info.storageUsed)));
  device.set("storageTotal", Json(static_cast<int>(info.storageTotal)));
  device.set("storageError", Json(storageError_));
  device.set("students", Json(static_cast<int>(students_.size())));
  Json open = Json::array();
  size_t openCount = 0;
  for (size_t k = 0; k < sessions_.size(); ++k) {
    if (isOpenState(effectiveState(sessions_[k], now))) ++openCount;
  }
  device.set("openSessions", Json(static_cast<int>(openCount)));
  root.set("device", device);

  Json students = Json::array();
  for (size_t k = 0; k < students_.size(); ++k) {
    Json s = Json::object();
    s.set("id", Json(static_cast<int>(students_[k].id)));
    s.set("number", Json::makeString(students_[k].number));
    s.set("name", Json::makeString(students_[k].name));
    s.set("section", Json::makeString(students_[k].section));
    students.push(s);
  }
  root.set("students", students);

  Json activities = Json::array();
  for (size_t k = 0; k < activities_.size(); ++k) activities.push(itemJson(activities_[k], true));
  root.set("activities", activities);

  Json exams = Json::array();
  for (size_t k = 0; k < exams_.size(); ++k) exams.push(itemJson(exams_[k], true));
  root.set("exams", exams);

  Json sessions = Json::array();
  for (size_t k = 0; k < sessions_.size(); ++k) sessions.push(sessionJson(sessions_[k], now));
  root.set("sessions", sessions);

  Json subs = Json::array();
  for (size_t k = 0; k < submissions_.size(); ++k) subs.push(submissionJson(submissions_[k]));
  root.set("submissions", subs);

  Json att = Json::array();
  for (size_t k = 0; k < attendance_.size(); ++k) {
    Json day = Json::object();
    day.set("date", Json::makeString(attendance_[k].date));
    day.set("section", Json::makeString(attendance_[k].section));
    Json marks = Json::array();
    for (size_t m = 0; m < attendance_[k].marks.size(); ++m) {
      Json mark = Json::object();
      mark.set("studentId", Json(static_cast<int>(attendance_[k].marks[m].first)));
      mark.set("status", Json::makeString(attendance_[k].marks[m].second));
      marks.push(mark);
    }
    day.set("marks", marks);
    att.push(day);
  }
  root.set("attendance", att);

  Json events = Json::array();
  for (size_t k = events_.size(); k > 0; --k) events.push(events_[k - 1]);
  root.set("events", events);

  Json settings = Json::object();
  settings.set("showCorrect", Json(showCorrect_));
  root.set("settings", settings);
  return root;
}

// ------------------------------------------------------------------ teacher --

Result Classroom::login(const std::string& pin, Ms now, std::string& token, Ms& expiresMs) {
  if (pin != teacherPin_) return Result::fail(401, "UNAUTHORIZED", "Incorrect teacher PIN.");
  if (teacherTokens_.size() >= limits::kTokens) teacherTokens_.erase(teacherTokens_.begin());
  TeacherToken t;
  t.token = newToken();
  t.expiresMs = now + kTeacherTokenTtlMs;
  teacherTokens_.push_back(t);
  token = t.token;
  expiresMs = t.expiresMs;
  logEvent(now, "info", "Teacher signed in");
  return Result::good();
}

Result Classroom::authorizeTeacher(const std::string& token, Ms now) const {
  for (size_t k = 0; k < teacherTokens_.size(); ++k) {
    if (teacherTokens_[k].token == token) {
      if (now >= teacherTokens_[k].expiresMs)
        return Result::fail(401, "TOKEN_EXPIRED", "Teacher session expired. Sign in again.");
      return Result::good();
    }
  }
  return Result::fail(401, "UNAUTHORIZED", "Teacher sign-in required.");
}

Result Classroom::addStudents(const Json& body, int& added, int& updated) {
  const Json* list = body.find("students");
  if (!list || !list->isArray()) return Result::fail(400, "BAD_REQUEST", "students[] is required.");
  if (list->items.size() > limits::kStudents)
    return Result::fail(400, "BAD_REQUEST", "At most 50 students per import.");
  added = 0;
  updated = 0;
  for (size_t k = 0; k < list->items.size(); ++k) {
    const Json& s = list->items[k];
    std::string number = s.str("number");
    std::string name = s.str("name");
    std::string section = s.str("section");
    if (!validText(number, 1, 20) || !validText(name, 1, 60) || !validSection(section))
      return Result::fail(400, "BAD_REQUEST", "Each student needs number (1-20), name (1-60), section.");
    Student* existing = findStudentByNumber(number);
    if (existing) {
      existing->name = name;
      existing->section = section;
      ++updated;
    } else {
      if (students_.size() >= limits::kStudents)
        return Result::fail(409, "CAPACITY_REACHED", "This controller supports 50 students.");
      Student st;
      st.id = nextStudentId_++;
      st.number = number;
      st.name = name;
      st.section = section;
      students_.push_back(st);
      ++added;
    }
  }
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the roster.");
  return Result::good();
}

Result Classroom::createActivity(const Json& body, unsigned& id) {
  if (activities_.size() >= limits::kActivities)
    return Result::fail(409, "CAPACITY_REACHED", "At most 16 activities.");
  std::string title = body.str("title");
  std::string target = body.str("target");
  std::string description = body.str("description");
  std::string instructions = body.str("instructions");
  if (!validText(title, 1, 80)) return Result::fail(400, "BAD_REQUEST", "Title is required (1-80).");
  if (!validSection(target)) return Result::fail(400, "BAD_REQUEST", "A valid section is required.");
  if (description.size() > 400 || instructions.size() > 400)
    return Result::fail(400, "BAD_REQUEST", "Description and instructions are limited to 400 characters.");
  Item a;
  a.id = nextItemId_++;
  a.kind = "activity";
  a.title = title;
  a.description = description;
  a.instructions = instructions;
  a.target = target;
  a.status = "draft";
  activities_.push_back(a);
  id = a.id;
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the activity.");
  return Result::good();
}

Result Classroom::createExam(const Json& body, unsigned& id) {
  if (exams_.size() >= limits::kExams)
    return Result::fail(409, "CAPACITY_REACHED", "At most 8 examinations.");
  std::string title = body.str("title");
  std::string target = body.str("target");
  std::string instructions = body.str("instructions");
  int duration = body.integer("durationMin", 15);
  int passing = body.integer("passingPct", 60);
  const Json* qs = body.find("questions");
  if (!validText(title, 1, 80)) return Result::fail(400, "BAD_REQUEST", "Title is required (1-80).");
  if (!validSection(target)) return Result::fail(400, "BAD_REQUEST", "A valid section is required.");
  if (instructions.size() > 400) return Result::fail(400, "BAD_REQUEST", "Instructions are limited to 400 characters.");
  if (duration < 1 || duration > 120) return Result::fail(400, "BAD_REQUEST", "Duration must be 1-120 minutes.");
  if (passing < 0 || passing > 100) return Result::fail(400, "BAD_REQUEST", "Passing score must be 0-100.");
  if (!qs || !qs->isArray() || qs->items.empty())
    return Result::fail(400, "BAD_REQUEST", "At least one question is required.");
  if (qs->items.size() > limits::kQuestions)
    return Result::fail(400, "BAD_REQUEST", "At most 20 questions per examination.");

  Item exam;
  exam.id = nextItemId_++;
  exam.kind = "exam";
  exam.title = title;
  exam.instructions = instructions;
  exam.target = target;
  exam.status = "draft";
  exam.durationMin = duration;
  exam.passingPct = passing;

  for (size_t k = 0; k < qs->items.size(); ++k) {
    const Json& qj = qs->items[k];
    Question q;
    q.id = nextQuestionId_++;
    q.type = qj.str("type", "mc");
    q.prompt = qj.str("prompt");
    if (!validText(q.prompt, 1, 300))
      return Result::fail(400, "BAD_REQUEST", "Each prompt is required (1-300).");
    if (q.type == "mc") {
      const Json* opts = qj.find("options");
      if (!opts || !opts->isArray() || opts->items.size() < 2 || opts->items.size() > limits::kOptions)
        return Result::fail(400, "BAD_REQUEST", "Multiple-choice questions need 2-6 options.");
      for (size_t x = 0; x < opts->items.size(); ++x) {
        if (!opts->items[x].isString() || !validText(opts->items[x].text, 1, 120))
          return Result::fail(400, "BAD_REQUEST", "Options are required (1-120).");
        q.options.push_back(opts->items[x].text);
      }
      q.correct = qj.integer("correct", -1);
      if (q.correct < 0 || static_cast<size_t>(q.correct) >= q.options.size())
        return Result::fail(400, "BAD_REQUEST", "Multiple-choice questions need a valid correct index.");
    } else if (q.type == "text") {
      const Json* keys = qj.find("keys");
      if (!keys || !keys->isArray() || keys->items.empty() || keys->items.size() > limits::kKeys)
        return Result::fail(400, "BAD_REQUEST", "Short-text questions need 1-4 accepted answers.");
      for (size_t x = 0; x < keys->items.size(); ++x) {
        if (!keys->items[x].isString() || !validText(keys->items[x].text, 1, 120))
          return Result::fail(400, "BAD_REQUEST", "Accepted answers are required (1-120).");
        q.keys.push_back(keys->items[x].text);
      }
    } else {
      return Result::fail(400, "BAD_REQUEST", "Question type must be mc or text.");
    }
    exam.questions.push_back(q);
  }

  exams_.push_back(exam);
  id = exam.id;
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the examination.");
  return Result::good();
}

Result Classroom::publishItem(const std::string& kind, unsigned id) {
  Item* item = findItem(kind, id);
  if (!item) return Result::fail(404, "NOT_FOUND", "No such item.");
  if (item->status == "completed")
    return Result::fail(409, "CONFLICT", "A completed item cannot be published again.");
  item->status = "active";
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the item.");
  return Result::good();
}

Result Classroom::completeItem(const std::string& kind, unsigned id, Ms now) {
  Item* item = findItem(kind, id);
  if (!item) return Result::fail(404, "NOT_FOUND", "No such item.");
  item->status = "completed";
  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& s = sessions_[k];
    if (s.refType == kind && s.refId == id && isOpenState(effectiveState(s, now))) {
      s.state = "ended";
      s.endedMs = now;
      finalizeAttemptsForSession(s.id, now, "SESSION_ENDED");
    }
  }
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the item.");
  return Result::good();
}

Result Classroom::createSession(const Json& body, Ms now, unsigned& id) {
  std::string refType = body.str("refType");
  unsigned refId = static_cast<unsigned>(body.integer("refId", 0));
  int expiresIn = body.integer("expiresInMin", 30);
  if (refType != "activity" && refType != "exam")
    return Result::fail(400, "BAD_REQUEST", "refType must be activity or exam.");
  Item* item = findItem(refType, refId);
  if (!item) return Result::fail(404, "NOT_FOUND", "No such item.");
  if (item->status != "active")
    return Result::fail(409, "ITEM_NOT_ACTIVE", "Publish the item before opening a session.");
  if (expiresIn < 5 || expiresIn > 180)
    return Result::fail(400, "BAD_REQUEST", "Session duration must be 5-180 minutes.");
  size_t open = 0;
  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& s = sessions_[k];
    if (!isOpenState(effectiveState(s, now))) continue;
    ++open;
    if (s.refType == refType && s.refId == refId)
      return Result::fail(409, "SESSION_EXISTS", "This item already has an open session.");
  }
  if (open >= limits::kOpenSessions)
    return Result::fail(409, "CAPACITY_REACHED", "At most 4 open sessions at once.");

  Session s;
  s.id = nextSessionId_++;
  s.code = newSessionCode();
  s.refType = refType;
  s.refId = refId;
  s.title = item->title;
  s.target = item->target;
  s.state = "ready";
  s.expiresInMin = static_cast<unsigned>(expiresIn);
  s.createdMs = now;
  s.expiresAtMs = now + static_cast<Ms>(expiresIn) * 60000ULL;
  sessions_.push_back(s);
  logEvent(now, "info", "Session " + s.code + " created");
  prune(now);
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the session.");
  id = s.id;
  return Result::good();
}

Result Classroom::sessionAction(unsigned id, const std::string& action, Ms now) {
  Session* s = findSession(id);
  if (!s) return Result::fail(404, "NOT_FOUND", "No such session.");
  std::string state = effectiveState(*s, now);
  Item* item = findItem(s->refType, s->refId);

  if (action == "start") {
    if (state == "live") return Result::good();
    if (state != "ready") return Result::fail(409, "SESSION_NOT_READY", "Only a ready session can start.");
    if (!item || item->status != "active")
      return Result::fail(409, "ITEM_NOT_ACTIVE", "The item behind this session is not active.");
    s->state = "live";
    s->startedMs = now;
    logEvent(now, "ok", "Session " + s->code + " started");
  } else if (action == "pause") {
    if (state == "paused") return Result::good();
    if (state != "live") return Result::fail(409, "SESSION_NOT_LIVE", "Only a live session can pause.");
    s->state = "paused";
    logEvent(now, "warn", "Session " + s->code + " paused");
  } else if (action == "resume") {
    if (state == "live") return Result::good();
    if (state != "paused") return Result::fail(409, "SESSION_NOT_PAUSED", "Only a paused session can resume.");
    s->state = "live";
    logEvent(now, "ok", "Session " + s->code + " resumed");
  } else if (action == "end") {
    if (state == "ended") return Result::good();
    s->state = "ended";
    s->endedMs = now;
    finalizeAttemptsForSession(s->id, now, "SESSION_ENDED");
    logEvent(now, "info", "Session " + s->code + " ended");
  } else {
    return Result::fail(400, "BAD_REQUEST", "Unknown session action.");
  }
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the session.");
  return Result::good();
}

Result Classroom::markAttendance(const Json& body) {
  std::string date = body.str("date");
  std::string section = body.str("section");
  if (date.size() != 10 || date[4] != '-' || date[7] != '-')
    return Result::fail(400, "BAD_REQUEST", "date must be YYYY-MM-DD.");
  for (size_t k = 0; k < date.size(); ++k) {
    if (k == 4 || k == 7) continue;
    if (date[k] < '0' || date[k] > '9') return Result::fail(400, "BAD_REQUEST", "date must be YYYY-MM-DD.");
  }
  if (!validSection(section)) return Result::fail(400, "BAD_REQUEST", "A valid section is required.");

  std::string markAll = body.str("markAll");
  std::string status = body.str("status");
  std::string one = markAll.empty() ? status : markAll;
  if (one != "present" && one != "late" && one != "absent" && one != "unmarked")
    return Result::fail(400, "BAD_REQUEST", "status must be present, late, absent or unmarked.");

  AttendanceDay* day = 0;
  for (size_t k = 0; k < attendance_.size(); ++k)
    if (attendance_[k].date == date && attendance_[k].section == section) day = &attendance_[k];
  if (!day) {
    AttendanceDay fresh;
    fresh.date = date;
    fresh.section = section;
    attendance_.push_back(fresh);
    day = &attendance_.back();
  }

  if (!markAll.empty()) {
    day->marks.clear();
    if (one != "unmarked") {
      for (size_t k = 0; k < students_.size(); ++k)
        if (inSection(students_[k], section)) day->marks.push_back(std::make_pair(students_[k].id, one));
    }
  } else {
    unsigned studentId = static_cast<unsigned>(body.integer("studentId", 0));
    Student* st = findStudent(studentId);
    if (!st) return Result::fail(404, "NOT_FOUND", "No such student.");
    if (st->section != section)
      return Result::fail(403, "FORBIDDEN", "That student is not in this section.");
    bool replaced = false;
    for (size_t k = 0; k < day->marks.size(); ++k) {
      if (day->marks[k].first == studentId) {
        if (one == "unmarked")
          day->marks.erase(day->marks.begin() + static_cast<long>(k));
        else
          day->marks[k].second = one;
        replaced = true;
        break;
      }
    }
    if (!replaced && one != "unmarked") day->marks.push_back(std::make_pair(studentId, one));
  }

  bool empty = day->marks.empty();
  std::string emptyDate = day->date, emptySection = day->section;
  if (empty) {
    for (size_t k = 0; k < attendance_.size(); ++k) {
      if (attendance_[k].date == emptyDate && attendance_[k].section == emptySection) {
        attendance_.erase(attendance_.begin() + static_cast<long>(k));
        break;
      }
    }
  }
  prune(0);
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save attendance.");
  return Result::good();
}

Result Classroom::reviewSubmission(unsigned id) {
  Submission* s = findSubmission(id);
  if (!s) return Result::fail(404, "NOT_FOUND", "No such submission.");
  if (s->refType != "activity")
    return Result::fail(409, "CONFLICT", "Examinations are graded automatically.");
  s->status = "reviewed";
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save the review.");
  return Result::good();
}

Result Classroom::setSettings(const Json& body) {
  showCorrect_ = body.flag("showCorrect", showCorrect_);
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save settings.");
  return Result::good();
}

Result Classroom::submissionDetail(unsigned id, Json& out) const {
  const Submission* s = findSubmission(id);
  if (!s) return Result::fail(404, "NOT_FOUND", "No such submission.");
  out = submissionJson(*s);
  std::string path;
  submissionPath(id, path);
  std::string raw = storage_ ? storage_->read(path) : std::string();
  if (!raw.empty()) {
    Json detail;
    std::string err;
    if (parseJson(raw, detail, &err) && detail.isObject()) {
      const Json* answers = detail.find("answers");
      if (answers) out.set("answers", *answers);
      out.set("text", Json::makeString(detail.str("text")));
    }
  }
  // Prompts are needed to render a submission; answer keys stay out unless the
  // caller is entitled to them (the student endpoint adds them when the teacher
  // allows correct-answer review).
  if (s->refType == "exam") {
    const Item* exam = findItem("exam", s->refId);
    if (exam) {
      const Json rendered = itemJson(*exam, false);
      const Json* questions = rendered.find("questions");
      if (questions) out.set("questions", *questions);
    }
  }
  return Result::good();
}

// ------------------------------------------------------------------ student --

Result Classroom::join(const std::string& code, const std::string& number, Ms now,
                       unsigned& studentId, unsigned& sessionId, std::string& token) {
  std::string normalized = code;
  for (size_t k = 0; k < normalized.size(); ++k)
    if (normalized[k] >= 'a' && normalized[k] <= 'z') normalized[k] = static_cast<char>(normalized[k] - 'a' + 'A');
  Session* s = findSessionByCode(normalized);
  if (!s) return Result::fail(404, "NOT_FOUND", "No session matches this code.");
  std::string state = effectiveState(*s, now);
  if (state == "ended" || state == "expired")
    return Result::fail(409, "SESSION_ENDED", "This session has ended.");
  Student* st = findStudentByNumber(number);
  if (!st) return Result::fail(404, "STUDENT_NOT_FOUND", "Your student number is not on this controller's roster.");
  if (st->section != s->target)
    return Result::fail(403, "SECTION_MISMATCH", "This session is for another section.");

  // A student belongs to at most one open session. Joining another one moves
  // them, which keeps recovery sane after a restart dropped their RAM token but
  // preserved membership. An examination in progress pins them in place.
  bool switched = false;
  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& other = sessions_[k];
    if (other.id == s->id || !isOpenState(effectiveState(other, now))) continue;
    bool member = false;
    for (size_t j = 0; j < other.joined.size(); ++j)
      if (other.joined[j] == st->id) member = true;
    if (!member) continue;
    for (size_t a = 0; a < attempts_.size(); ++a) {
      if (attempts_[a].studentId == st->id && attempts_[a].sessionId == other.id)
        return Result::fail(409, "EXAM_IN_PROGRESS",
                           "Finish your examination before joining another session.");
    }
    for (size_t j = 0; j < other.joined.size(); ++j) {
      if (other.joined[j] == st->id) {
        other.joined.erase(other.joined.begin() + static_cast<long>(j));
        switched = true;
        break;
      }
    }
  }
  if (switched) logEvent(now, "info", st->name + " switched to " + s->code);

  bool already = false;
  for (size_t k = 0; k < s->joined.size(); ++k)
    if (s->joined[k] == st->id) already = true;
  if (!already) {
    if (s->joined.size() >= limits::kStudents)
      return Result::fail(409, "CAPACITY_REACHED", "This session is full.");
    s->joined.push_back(st->id);
    logEvent(now, "ok", st->name + " joined " + s->code);
    if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not record your join.");
  }

  if (studentTokens_.size() >= limits::kTokens) studentTokens_.erase(studentTokens_.begin());
  StudentToken t;
  t.token = newToken();
  t.studentId = st->id;
  t.sessionId = s->id;
  t.expiresMs = now + kStudentTokenTtlMs;
  studentTokens_.push_back(t);
  token = t.token;
  studentId = st->id;
  sessionId = s->id;
  return Result::good();
}

Result Classroom::sessionPreview(const std::string& code, Ms now, Json& out) const {
  std::string normalized = code;
  for (size_t k = 0; k < normalized.size(); ++k)
    if (normalized[k] >= 'a' && normalized[k] <= 'z')
      normalized[k] = static_cast<char>(normalized[k] - 'a' + 'A');
  const Session* found = 0;
  for (size_t k = 0; k < sessions_.size(); ++k)
    if (sessions_[k].code == normalized) found = &sessions_[k];
  if (!found) return Result::fail(404, "NOT_FOUND", "No session matches this code.");
  out = Json::object();
  out.set("ok", Json(true));
  Json s = Json::object();
  s.set("code", Json::makeString(found->code));
  s.set("title", Json::makeString(found->title));
  s.set("refType", Json::makeString(found->refType));
  s.set("target", Json::makeString(found->target));
  s.set("state", Json::makeString(effectiveState(*found, now)));
  s.set("expiresInSec",
        Json(static_cast<int>(found->expiresAtMs > now ? (found->expiresAtMs - now) / 1000 : 0)));
  out.set("session", s);
  return Result::good();
}

Result Classroom::authorizeStudent(const std::string& token, Ms now, unsigned& studentId,
                                   unsigned& sessionId) const {
  for (size_t k = 0; k < studentTokens_.size(); ++k) {
    const StudentToken& t = studentTokens_[k];
    if (t.token != token) continue;
    if (now >= t.expiresMs)
      return Result::fail(401, "TOKEN_EXPIRED", "Your session token expired. Join again.");
    const Session* s = findSession(t.sessionId);
    if (!s) return Result::fail(401, "SESSION_CLOSED", "This session is no longer available.");
    const std::string state = effectiveState(*s, now);
    if ((state == "ended" || state == "expired") && s->endedMs && now > s->endedMs + kSessionGraceMs)
      return Result::fail(401, "SESSION_CLOSED", "This session has ended.");
    bool stillMember = false;
    for (size_t j = 0; j < s->joined.size(); ++j)
      if (s->joined[j] == t.studentId) stillMember = true;
    if (!stillMember)
      return Result::fail(401, "NOT_IN_SESSION", "You are no longer in this session. Join again.");
    studentId = t.studentId;
    sessionId = t.sessionId;
    return Result::good();
  }
  return Result::fail(401, "UNAUTHORIZED", "Join a session first.");
}

Result Classroom::studentSnapshot(unsigned studentId, Ms now, Json& out) {
  const Student* st = findStudent(studentId);
  if (!st) return Result::fail(401, "UNAUTHORIZED", "Unknown student.");
  const Session* session = 0;
  for (size_t k = 0; k < sessions_.size(); ++k) {
    const Session& s = sessions_[k];
    if (!isOpenState(effectiveState(s, now))) continue;
    for (size_t j = 0; j < s.joined.size(); ++j)
      if (s.joined[j] == studentId) session = &s;
  }

  out = Json::object();
  out.set("ok", Json(true));
  Json me = Json::object();
  me.set("id", Json(static_cast<int>(st->id)));
  me.set("number", Json::makeString(st->number));
  me.set("name", Json::makeString(st->name));
  me.set("section", Json::makeString(st->section));
  out.set("me", me);

  if (session) {
    out.set("session", sessionJson(*session, now));
  } else {
    out.set("session", Json());
  }

  Json work = Json::array();
  for (int pass = 0; pass < 2; ++pass) {
    const std::vector<Item>& list = pass == 0 ? exams_ : activities_;
    for (size_t k = 0; k < list.size(); ++k) {
      const Item& item = list[k];
      if (item.target != st->section) continue;
      bool relevant = item.status == "active";
      if (session && session->refType == item.kind && session->refId == item.id) relevant = true;
      if (!relevant) continue;
      Json o = Json::object();
      o.set("kind", Json::makeString(item.kind));
      o.set("id", Json(static_cast<int>(item.id)));
      o.set("title", Json::makeString(item.title));
      o.set("instructions", Json::makeString(item.instructions));
      o.set("durationMin", Json(item.durationMin));
      o.set("passingPct", Json(item.passingPct));
      o.set("questionCount", Json(static_cast<int>(item.questions.size())));
      o.set("status", Json::makeString(item.status));
      o.set("submitted", Json(findSubmissionFor(st->id, item.kind, item.id) != 0));
      work.push(o);
    }
  }
  out.set("availableWork", work);

  Json subs = Json::array();
  for (size_t k = 0; k < submissions_.size(); ++k) {
    if (submissions_[k].studentId != st->id) continue;
    Json s = submissionJson(submissions_[k]);
    s.set("answersVisible", Json(showCorrect_ || submissions_[k].refType != "exam"));
    subs.push(s);
  }
  out.set("mySubmissions", subs);

  Json attemptOut = Json();
  for (size_t k = 0; k < attempts_.size(); ++k) {
    if (attempts_[k].studentId != st->id) continue;
    Json a = Json::object();
    a.set("examId", Json(static_cast<int>(attempts_[k].examId)));
    a.set("sessionId", Json(static_cast<int>(attempts_[k].sessionId)));
    a.set("remainingSec", Json(static_cast<int>(attempts_[k].remainingMs / 1000)));
    attemptOut = a;
  }
  out.set("activeAttempt", attemptOut);

  Json policy = Json::object();
  policy.set("showCorrect", Json(showCorrect_));
  out.set("resultsPolicy", policy);
  return Result::good();
}

Result Classroom::startExam(unsigned studentId, unsigned examId, Ms now, Json& out) {
  Student* st = findStudent(studentId);
  if (!st) return Result::fail(401, "UNAUTHORIZED", "Unknown student.");
  const Item* exam = findItem("exam", examId);
  if (!exam) return Result::fail(404, "NOT_FOUND", "No such examination.");
  if (exam->target != st->section)
    return Result::fail(403, "FORBIDDEN", "This examination is for another section.");
  if (findSubmissionFor(studentId, "exam", examId))
    return Result::fail(423, "ALREADY_SUBMITTED", "You already submitted this examination.");

  Session* session = 0;
  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& s = sessions_[k];
    if (!isOpenState(effectiveState(s, now))) continue;
    for (size_t j = 0; j < s.joined.size(); ++j)
      if (s.joined[j] == studentId) session = &s;
  }
  if (!session) return Result::fail(403, "NOT_IN_SESSION", "Join the session before starting.");
  if (session->refType != "exam" || session->refId != examId)
    return Result::fail(403, "WRONG_SESSION", "Your session is for a different activity.");
  std::string state = effectiveState(*session, now);
  if (state != "live")
    return Result::fail(409, "SESSION_NOT_LIVE", "Wait for your teacher to start the session.");

  Attempt* attempt = findAttempt(studentId, examId);
  if (!attempt) {
    if (attempts_.size() >= limits::kAttempts)
      return Result::fail(409, "CAPACITY_REACHED", "Too many examinations in progress.");
    Attempt fresh;
    fresh.examId = examId;
    fresh.sessionId = session->id;
    fresh.studentId = studentId;
    fresh.remainingMs = static_cast<Ms>(exam->durationMin) * 60000ULL;
    fresh.lastMs = now;
    attempts_.push_back(fresh);
    attempt = &attempts_.back();
    logEvent(now, "info", st->name + " started " + exam->title);
    if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not start the examination.");
  } else if (attempt->lastMs == 0) {
    attempt->lastMs = now;
  }

  out = Json::object();
  out.set("ok", Json(true));
  Json a = Json::object();
  a.set("examId", Json(static_cast<int>(exam->id)));
  a.set("title", Json::makeString(exam->title));
  a.set("durationMin", Json(exam->durationMin));
  a.set("passingPct", Json(exam->passingPct));
  a.set("remainingSec", Json(static_cast<int>(attempt->remainingMs / 1000)));
  Json qs = Json::array();
  for (size_t k = 0; k < exam->questions.size(); ++k) {
    const Question& q = exam->questions[k];
    Json qo = Json::object();
    qo.set("id", Json(static_cast<int>(q.id)));
    qo.set("type", Json::makeString(q.type));
    qo.set("prompt", Json::makeString(q.prompt));
    Json opts = Json::array();
    for (size_t x = 0; x < q.options.size(); ++x) opts.push(Json::makeString(q.options[x]));
    qo.set("options", opts);
    qs.push(qo);
  }
  a.set("questions", qs);

  // Restore any answers autosaved before a disconnect or app restart.
  std::string path;
  answersPath(examId, studentId, path);
  std::string raw = storage_->read(path);
  Json saved = Json();
  if (!raw.empty() && raw.size() <= kMaxAnswerBytes) {
    std::string err;
    parseJson(raw, saved, &err);
  }
  a.set("answers", saved.isObject() ? saved : Json::object());
  out.set("attempt", a);
  return Result::good();
}

Result Classroom::saveAnswers(unsigned studentId, unsigned examId, const Json& answers, Ms now) {
  Attempt* attempt = findAttempt(studentId, examId);
  if (!attempt) return Result::fail(409, "EXAM_NOT_STARTED", "Start the examination first.");
  const Item* exam = findItem("exam", examId);
  if (!exam) return Result::fail(404, "NOT_FOUND", "No such examination.");
  Session* session = findSession(attempt->sessionId);
  if (!session || !sessionAcceptsWork(*session, now))
    return Result::fail(409, "SESSION_NOT_LIVE", "The session is not live.");
  Json normalized;
  Result v = validateAnswers(*exam, answers, false, normalized);
  if (!v.ok) return v;
  std::string path;
  answersPath(examId, studentId, path);
  if (!storage_->writeAtomic(path, normalized.dump()))
    return Result::fail(500, "STORAGE_FULL", "Could not save your answers.");
  return Result::good();
}

Result Classroom::validateAnswers(const Item& exam, const Json& answers, bool requireAll,
                                  Json& normalized) const {
  if (!answers.isObject()) return Result::fail(400, "BAD_REQUEST", "answers must be an object.");
  if (answers.members.size() > exam.questions.size())
    return Result::fail(400, "BAD_REQUEST", "Too many answers for this examination.");
  // Reject answers for questions that are not part of this examination, so a
  // client cannot smuggle arbitrary keys into stored work.
  for (size_t m = 0; m < answers.members.size(); ++m) {
    bool known = false;
    for (size_t k = 0; k < exam.questions.size(); ++k) {
      if (answers.members[m].first == formatCode(exam.questions[k].id)) {
        known = true;
        break;
      }
    }
    if (!known) return Result::fail(400, "BAD_REQUEST", "Answer for an unknown question.");
  }
  normalized = Json::object();
  for (size_t k = 0; k < exam.questions.size(); ++k) {
    const Question& q = exam.questions[k];
    std::string key = formatCode(q.id);
    const Json* value = answers.find(key.c_str());
    if (!value) {
      if (requireAll) {
        Result r = Result::fail(400, "UNANSWERED_QUESTIONS", "Answer every question before submitting.");
        r.hint = key;
        return r;
      }
      continue;
    }
    if (q.type == "mc") {
      if (!value->isNumber()) return Result::fail(400, "BAD_REQUEST", "Multiple-choice answers must be numbers.");
      int idx = static_cast<int>(value->number);
      if (idx < 0 || static_cast<size_t>(idx) >= q.options.size())
        return Result::fail(400, "BAD_REQUEST", "Answer index out of range.");
      normalized.set(key, Json(idx));
    } else {
      if (!value->isString()) return Result::fail(400, "BAD_REQUEST", "Short-text answers must be text.");
      if (!validText(value->text, 0, 200))
        return Result::fail(400, "BAD_REQUEST", "Short-text answers are limited to 200 characters.");
      normalized.set(key, Json::makeString(value->text));
    }
  }
  if (normalized.dump().size() > kMaxAnswerBytes)
    return Result::fail(400, "BAD_REQUEST", "Answers are too large.");
  return Result::good();
}

Result Classroom::gradeAndStore(const Item& exam, const Json& answers, const Session& session,
                                unsigned studentId, bool timeout, Ms now, Json& out) {
  Submission* existing = findSubmissionFor(studentId, "exam", exam.id);
  if (existing) {
    out = Json::object();
    out.set("ok", Json(true));
    out.set("duplicate", Json(true));
    out.set("submission", submissionJson(*existing));
    return Result::good();
  }
  if (submissions_.size() >= limits::kSubmissions)
    return Result::fail(500, "STORAGE_FULL", "The controller cannot store more submissions.");

  int score = 0;
  for (size_t k = 0; k < exam.questions.size(); ++k) {
    const Question& q = exam.questions[k];
    std::string key = formatCode(q.id);
    const Json* value = answers.find(key.c_str());
    if (!value) continue;
    if (q.type == "mc") {
      if (value->isNumber() && static_cast<int>(value->number) == q.correct) ++score;
    } else if (value->isString()) {
      std::string given = trimLower(value->text);
      for (size_t x = 0; x < q.keys.size(); ++x) {
        if (trimLower(q.keys[x]) == given) {
          ++score;
          break;
        }
      }
    }
  }
  int total = static_cast<int>(exam.questions.size());
  int percent = total > 0 ? static_cast<int>((static_cast<long>(score) * 100 + total / 2) / total) : 0;

  Submission sub;
  sub.id = nextSubmissionId_++;
  sub.refType = "exam";
  sub.refId = exam.id;
  sub.sessionId = session.id;
  sub.studentId = studentId;
  sub.score = score;
  sub.total = total;
  sub.percent = percent;
  sub.passed = percent >= exam.passingPct;
  sub.timeout = timeout;
  sub.status = "graded";
  sub.submittedMs = now;

  Json payload = Json::object();
  payload.set("answers", answers);
  std::string path;
  submissionPath(sub.id, path);
  if (!storage_->writeAtomic(path, payload.dump()))
    return Result::fail(500, "STORAGE_FULL", "Could not save your submission.");
  submissions_.push_back(sub);

  std::string attPath;
  answersPath(exam.id, studentId, attPath);
  storage_->remove(attPath);
  for (size_t k = 0; k < attempts_.size();) {
    if (attempts_[k].studentId == studentId && attempts_[k].examId == exam.id)
      attempts_.erase(attempts_.begin() + static_cast<long>(k));
    else
      ++k;
  }

  const Student* st = findStudent(studentId);
  logEvent(now, "ok", (st ? st->name : std::string("Student")) + " submitted " + exam.title);
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save your submission.");

  out = Json::object();
  out.set("ok", Json(true));
  out.set("duplicate", Json(false));
  out.set("submission", submissionJson(sub));
  return Result::good();
}

Result Classroom::submit(unsigned studentId, const Json& body, Ms now, Json& out) {
  std::string refType = body.str("refType");
  unsigned refId = static_cast<unsigned>(body.integer("refId", 0));
  if (refType != "activity" && refType != "exam")
    return Result::fail(400, "BAD_REQUEST", "refType must be activity or exam.");
  Item* item = findItem(refType, refId);
  if (!item) return Result::fail(404, "NOT_FOUND", "No such item.");
  const Student* st = findStudent(studentId);
  if (!st) return Result::fail(401, "UNAUTHORIZED", "Unknown student.");
  if (item->target != st->section)
    return Result::fail(403, "FORBIDDEN", "This work is for another section.");

  // Idempotency comes first: a client retrying after a lost response (or after a
  // restart that dropped its attempt) gets the original submission, never a
  // duplicate record and never a confusing "not started" error.
  Submission* previous = findSubmissionFor(studentId, refType, refId);
  if (previous) {
    out = Json::object();
    out.set("ok", Json(true));
    out.set("duplicate", Json(true));
    out.set("submission", submissionJson(*previous));
    return Result::good();
  }

  Session* session = 0;
  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& s = sessions_[k];
    if (!isOpenState(effectiveState(s, now))) continue;
    for (size_t j = 0; j < s.joined.size(); ++j)
      if (s.joined[j] == studentId) session = &s;
  }
  if (!session) return Result::fail(403, "NOT_IN_SESSION", "Join the session first.");
  if (session->refType != refType || session->refId != refId)
    return Result::fail(403, "WRONG_SESSION", "Your session is for different work.");
  if (!sessionAcceptsWork(*session, now))
    return Result::fail(409, "SESSION_NOT_LIVE", "The session is not live right now.");

  if (refType == "exam" && item->questions.empty())
    return Result::fail(409, "EXAM_EMPTY", "This examination has no questions.");

  if (refType == "exam") {
    Attempt* attempt = findAttempt(studentId, refId);
    bool timedOut = false;
    Json answers = Json();
    const Json* provided = body.find("answers");
    if (provided && provided->isObject()) {
      answers = *provided;
    } else {
      std::string path;
      answersPath(refId, studentId, path);
      std::string raw = storage_->read(path);
      if (!raw.empty()) {
        std::string err;
        parseJson(raw, answers, &err);
      }
    }
    if (!attempt) {
      if (!answers.isObject())
        return Result::fail(409, "EXAM_NOT_STARTED", "Start the examination first.");
      // Accept an explicit submission even if the attempt record was lost, as
      // long as the student belongs to a live session for this exam.
      timedOut = true;
    } else if (attempt->remainingMs == 0) {
      timedOut = true;
    }
    Json normalized;
    Result v = validateAnswers(*item, answers, !timedOut, normalized);
    if (!v.ok) return v;
    return gradeAndStore(*item, normalized, *session, studentId, timedOut, now, out);
  }

  Submission* existing = findSubmissionFor(studentId, "activity", refId);
  if (existing) {
    out = Json::object();
    out.set("ok", Json(true));
    out.set("duplicate", Json(true));
    out.set("submission", submissionJson(*existing));
    return Result::good();
  }
  std::string text = body.str("text");
  const Json* textNode = body.find("text");
  if (!textNode || !textNode->isString())
    return Result::fail(400, "BAD_REQUEST", "A text response is required.");
  if (!validText(textNode->text, 10, 500))
    return Result::fail(400, "BAD_REQUEST", "Write between 10 and 500 characters.");
  if (submissions_.size() >= limits::kSubmissions)
    return Result::fail(500, "STORAGE_FULL", "The controller cannot store more submissions.");

  Submission sub;
  sub.id = nextSubmissionId_++;
  sub.refType = "activity";
  sub.refId = refId;
  sub.sessionId = session->id;
  sub.studentId = studentId;
  sub.total = 0;
  sub.percent = 0;
  sub.passed = false;
  sub.status = "pending";
  sub.submittedMs = now;
  Json payload = Json::object();
  payload.set("text", Json::makeString(text));
  std::string path;
  submissionPath(sub.id, path);
  if (!storage_->writeAtomic(path, payload.dump()))
    return Result::fail(500, "STORAGE_FULL", "Could not save your response.");
  submissions_.push_back(sub);
  logEvent(now, "ok", st->name + " submitted " + item->title);
  if (!save()) return Result::fail(500, "STORAGE_FULL", "Could not save your response.");

  out = Json::object();
  out.set("ok", Json(true));
  out.set("duplicate", Json(false));
  out.set("submission", submissionJson(sub));
  return Result::good();
}

Result Classroom::leave(unsigned studentId, Ms now) {
  for (size_t k = 0; k < attempts_.size(); ++k) {
    if (attempts_[k].studentId == studentId && attempts_[k].remainingMs > 0 &&
        attempts_[k].lastMs + 1000 >= now)
      return Result::fail(409, "EXAM_IN_PROGRESS", "Finish your examination before leaving.");
  }
  bool changed = false;
  const Student* st = findStudent(studentId);
  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& s = sessions_[k];
    for (size_t j = 0; j < s.joined.size();) {
      if (s.joined[j] == studentId) {
        s.joined.erase(s.joined.begin() + static_cast<long>(j));
        changed = true;
        if (st && isOpenState(effectiveState(s, now)))
          logEvent(now, "info", st->name + " left " + s.code);
      } else {
        ++j;
      }
    }
  }
  clearStudentTokens(studentId);
  if (changed && !save()) return Result::fail(500, "STORAGE_FULL", "Could not leave the session.");
  return Result::good();
}

Result Classroom::ownSubmission(unsigned studentId, unsigned id, Json& out) const {
  const Submission* s = findSubmission(id);
  if (!s) return Result::fail(404, "NOT_FOUND", "No such submission.");
  if (s->studentId != studentId) return Result::fail(403, "FORBIDDEN", "This is not your submission.");
  Result r = submissionDetail(id, out);
  if (!r.ok) return r;
  bool visible = showCorrect_ || s->refType != "exam";
  out.set("answersVisible", Json(visible));
  if (visible && s->refType == "exam") {
    const Item* exam = findItem("exam", s->refId);
    if (exam) {
      const Json rendered = itemJson(*exam, true);
      const Json* questions = rendered.find("questions");
      if (questions) out.set("questions", *questions);
    }
  }
  return Result::good();
}

// --------------------------------------------------------------- exam clock --

void Classroom::finalizeAttempt(Attempt& attempt, Ms now, const char* reason) {
  Item* exam = findItem("exam", attempt.examId);
  Session* session = findSession(attempt.sessionId);
  if (!exam || !session) return;
  Json answers = Json::object();
  std::string path;
  answersPath(attempt.examId, attempt.studentId, path);
  std::string raw = storage_->read(path);
  if (!raw.empty()) {
    Json parsed;
    std::string err;
    if (parseJson(raw, parsed, &err) && parsed.isObject()) answers = parsed;
  }
  Json normalized;
  if (!validateAnswers(*exam, answers, false, normalized).ok) normalized = Json::object();
  Json out;
  gradeAndStore(*exam, normalized, *session, attempt.studentId, true, now, out);
  (void)reason;
}

void Classroom::finalizeAttemptsForSession(unsigned sessionId, Ms now, const char* reason) {
  std::vector<Attempt> pending;
  for (size_t k = 0; k < attempts_.size();) {
    if (attempts_[k].sessionId == sessionId) {
      pending.push_back(attempts_[k]);
      attempts_.erase(attempts_.begin() + static_cast<long>(k));
    } else {
      ++k;
    }
  }
  for (size_t k = 0; k < pending.size(); ++k) finalizeAttempt(pending[k], now, reason);
}

void Classroom::tick(Ms now) {
  bool changed = false;
  for (size_t k = 0; k < attempts_.size(); ++k) {
    Attempt& a = attempts_[k];
    Session* s = findSession(a.sessionId);
    if (!s) continue;
    if (a.lastMs == 0 || now < a.lastMs) a.lastMs = now;
    bool live = effectiveState(*s, now) == "live";
    if (live && a.remainingMs > 0) {
      Ms delta = now - a.lastMs;
      a.remainingMs = (delta >= a.remainingMs) ? 0 : a.remainingMs - delta;
    }
    a.lastMs = now;
    if (a.remainingMs == 0) changed = true;
  }

  // Finalize timed-out attempts outside the loop (mutates attempts_).
  for (size_t k = 0; k < attempts_.size();) {
    if (attempts_[k].remainingMs == 0) {
      Attempt a = attempts_[k];
      attempts_.erase(attempts_.begin() + static_cast<long>(k));
      finalizeAttempt(a, now, "TIMEOUT");
      changed = true;
    } else {
      ++k;
    }
  }

  for (size_t k = 0; k < sessions_.size(); ++k) {
    Session& s = sessions_[k];
    if (isOpenState(s.state) && s.expiresAtMs > 0 && now >= s.expiresAtMs) {
      s.state = "expired";
      s.endedMs = now;
      finalizeAttemptsForSession(s.id, now, "SESSION_EXPIRED");
      logEvent(now, "warn", "Session " + s.code + " expired");
      changed = true;
    }
  }

  for (size_t k = 0; k < teacherTokens_.size();) {
    if (now >= teacherTokens_[k].expiresMs)
      teacherTokens_.erase(teacherTokens_.begin() + static_cast<long>(k));
    else
      ++k;
  }
  for (size_t k = 0; k < studentTokens_.size();) {
    if (now >= studentTokens_[k].expiresMs)
      studentTokens_.erase(studentTokens_.begin() + static_cast<long>(k));
    else
      ++k;
  }

  prune(now);
  if (changed) save();
}

void Classroom::prune(Ms now) {
  if (attendance_.size() > limits::kAttendanceDays) {
    std::sort(attendance_.begin(), attendance_.end(),
              [](const AttendanceDay& a, const AttendanceDay& b) { return a.date < b.date; });
    while (attendance_.size() > limits::kAttendanceDays) attendance_.erase(attendance_.begin());
  }
  if (sessions_.size() > limits::kSessions) {
    for (size_t k = 0; k < sessions_.size() && sessions_.size() > limits::kSessions;) {
      if (!isOpenState(effectiveState(sessions_[k], now))) {
        sessions_.erase(sessions_.begin() + static_cast<long>(k));
      } else {
        ++k;
      }
    }
  }
}

size_t Classroom::openSessionCount() const {
  size_t n = 0;
  for (size_t k = 0; k < sessions_.size(); ++k)
    if (isOpenState(sessions_[k].state)) ++n;
  return n;
}

}  // namespace sntch

