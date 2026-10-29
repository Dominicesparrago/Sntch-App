#include "sntch/api.hpp"

#include <cstdlib>
#include <cstring>

namespace sntch {
namespace {

const size_t kMaxBodyBytes = 16 * 1024;
const int kRateLimitPerMinute = 120;
const size_t kRateLimitClients = 24;

std::vector<std::string> splitPath(const std::string& path) {
  std::vector<std::string> parts;
  std::string current;
  for (size_t k = 0; k < path.size(); ++k) {
    char c = path[k];
    if (c == '/') {
      if (!current.empty()) parts.push_back(current);
      current.clear();
    } else if (c == '?') {
      break;
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

bool toId(const std::string& text, unsigned& out) {
  if (text.empty() || text.size() > 9) return false;
  unsigned value = 0;
  for (size_t k = 0; k < text.size(); ++k) {
    if (text[k] < '0' || text[k] > '9') return false;
    value = value * 10 + static_cast<unsigned>(text[k] - '0');
  }
  if (value == 0) return false;
  out = value;
  return true;
}

Json errorBody(const char* code, const std::string& message) {
  Json root = Json::object();
  root.set("ok", Json(false));
  Json err = Json::object();
  err.set("code", Json::makeString(code));
  err.set("message", Json::makeString(message));
  root.set("error", err);
  return root;
}

}  // namespace

Api::Api(Classroom* classroom, const DeviceInfo& device) : classroom_(classroom), device_(device) {}

Response Api::json(int status, const Json& body) {
  Response r;
  r.status = status;
  r.body = body.dump();
  r.contentType = "application/json";
  return r;
}

Response Api::errorResponse(const Result& result) {
  Response r = json(result.status, errorBody(result.code.c_str(), result.message));
  return r;
}

Response Api::notFound() {
  return json(404, errorBody("NOT_FOUND", "Unknown endpoint."));
}

std::string Api::bearer(const std::string& authHeader) const {
  const std::string prefix = "Bearer ";
  if (authHeader.compare(0, prefix.size(), prefix) == 0)
    return authHeader.substr(prefix.size());
  return authHeader;  // tolerate a bare token from simple clients
}

bool Api::rateLimited(const Request& request) {
  if (request.ip.empty()) return false;
  for (size_t k = 0; k < windows_.size(); ++k) {
    Window& w = windows_[k];
    if (w.ip != request.ip) continue;
    if (request.nowMs < w.startMs || request.nowMs - w.startMs >= 60000) {
      w.startMs = request.nowMs;
      w.count = 1;
      return false;
    }
    ++w.count;
    return w.count > kRateLimitPerMinute;
  }
  Window w;
  w.ip = request.ip;
  w.startMs = request.nowMs;
  w.count = 1;
  if (windows_.size() >= kRateLimitClients) windows_.erase(windows_.begin());
  windows_.push_back(w);
  return false;
}

namespace {
bool parseBody(const Request& request, Json& out, Response& failure) {
  if (request.body.empty()) {
    out = Json::object();
    return true;
  }
  std::string err;
  if (!parseJson(request.body, out, &err) || !out.isObject()) {
    failure = Response();
    failure.status = 400;
    Json root = errorBody("BAD_REQUEST", err.empty() ? "Body must be a JSON object." : err.c_str());
    failure.body = root.dump();
    return false;
  }
  return true;
}
}  // namespace

Response Api::handle(const Request& raw) {
  Request request = raw;
  if (request.path.size() > 256) return json(414, errorBody("BAD_REQUEST", "Path too long."));
  if (request.body.size() > kMaxBodyBytes)
    return json(413, errorBody("PAYLOAD_TOO_LARGE", "Request body exceeds 16 KiB."));
  if (rateLimited(request))
    return json(429, errorBody("RATE_LIMITED", "Too many requests. Slow down."));

  if (classroom_) classroom_->tick(request.nowMs);

  std::vector<std::string> parts = splitPath(request.path);
  if (parts.size() < 2 || parts[0] != "api") return notFound();

  if (parts[1] == "health") {
    if (request.method != "GET") return json(405, errorBody("BAD_REQUEST", "Use GET."));
    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("firmware", Json::makeString(device_.firmware));
    root.set("deviceName", Json::makeString(device_.name));
    root.set("ssid", Json::makeString(device_.ssid));
    root.set("ip", Json::makeString(device_.ip));
    root.set("uptimeMs", Json(static_cast<double>(request.nowMs)));
    root.set("students", Json(classroom_ ? static_cast<int>(classroom_->studentCount()) : 0));
    root.set("activeSessions",
             Json(classroom_ ? static_cast<int>(classroom_->openSessionCount()) : 0));
    root.set("freeHeap", Json(static_cast<int>(device_.freeHeap)));
    root.set("storageUsed", Json(static_cast<int>(device_.storageUsed)));
    root.set("storageTotal", Json(static_cast<int>(device_.storageTotal)));
    root.set("storageError", Json(classroom_ ? classroom_->storageError() : true));
    return json(200, root);
  }

  if (parts[1] == "session" && parts.size() == 3) {
    if (request.method != "GET") return json(405, errorBody("BAD_REQUEST", "Use GET."));
    Json out;
    Result r = classroom_->sessionPreview(parts[2], request.nowMs, out);
    if (!r.ok) return errorResponse(r);
    return json(200, out);
  }

  if (parts[1] == "teacher") return teacher(request, parts);
  if (parts[1] == "student") return student(request, parts);
  return notFound();
}

Response Api::teacher(const Request& request, const std::vector<std::string>& p) {
  if (!classroom_) return json(500, errorBody("INTERNAL", "Controller is not initialised."));

  if (p.size() == 3 && p[2] == "login" && request.method == "POST") {
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    std::string token;
    Ms expires = 0;
    Result r = classroom_->login(body.str("pin"), request.nowMs, token, expires);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("token", Json::makeString(token));
    root.set("expiresInSec", Json(static_cast<int>((expires - request.nowMs) / 1000)));
    Json device = Json::object();
    device.set("name", Json::makeString(device_.name));
    device.set("ssid", Json::makeString(device_.ssid));
    device.set("ip", Json::makeString(device_.ip));
    device.set("firmware", Json::makeString(device_.firmware));
    root.set("device", device);
    return json(200, root);
  }

  Result auth = classroom_->authorizeTeacher(bearer(request.auth), request.nowMs);
  if (!auth.ok) return errorResponse(auth);

  if (p.size() == 3 && p[2] == "state") {
    if (request.method != "GET") return json(405, errorBody("BAD_REQUEST", "Use GET."));
    return json(200, classroom_->teacherSnapshot(request.nowMs, device_));
  }

  if (p.size() == 3 && p[2] == "deletions") {
    if (request.method != "GET") return json(405, errorBody("BAD_REQUEST", "Use GET."));
    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("pendingDeletions", classroom_->pendingDeletionsJson());
    return json(200, root);
  }

  if (p.size() == 4 && p[2] == "deletions") {
    unsigned id = 0;
    if (!toId(p[3], id)) return json(400, errorBody("BAD_REQUEST", "Invalid session id."));
    if (request.method != "GET") return json(405, errorBody("BAD_REQUEST", "Use GET."));
    Json backup;
    Result r = classroom_->deletionBackup(id, backup);
    if (!r.ok) return errorResponse(r);
    backup.set("ok", Json(true));
    backup.set("sessionId", Json(static_cast<int>(id)));
    return json(200, backup);
  }

  if (p.size() == 5 && p[2] == "deletions") {
    unsigned id = 0;
    if (!toId(p[3], id)) return json(400, errorBody("BAD_REQUEST", "Invalid session id."));
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json out;
    Result r;
    if (p[4] == "ack") {
      r = classroom_->acknowledgeDeletion(id, request.nowMs, out);
    } else if (p[4] == "cancel") {
      r = classroom_->cancelDeletion(id, request.nowMs, out);
    } else {
      return notFound();
    }
    if (!r.ok) return errorResponse(r);
    return json(200, out);
  }

  if (p.size() == 3 && p[2] == "students") {
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    int added = 0, updated = 0;
    Result r = classroom_->addStudents(body, added, updated);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("added", Json(added));
    root.set("updated", Json(updated));
    return json(200, root);
  }

  if (p.size() == 3 && (p[2] == "activities" || p[2] == "exams")) {
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    unsigned id = 0;
    Result r = (p[2] == "exams") ? classroom_->createExam(body, id) : classroom_->createActivity(body, id);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("id", Json(static_cast<int>(id)));
    return json(200, root);
  }

  if (p.size() == 5 && (p[2] == "activities" || p[2] == "exams")) {
    unsigned id = 0;
    if (!toId(p[3], id)) return json(400, errorBody("BAD_REQUEST", "Invalid item id."));
    std::string kind = (p[2] == "exams") ? "exam" : "activity";
    if (p[4] == "publish") {
      Result r = classroom_->publishItem(kind, id);
      if (!r.ok) return errorResponse(r);
    } else if (p[4] == "complete") {
      Result r = classroom_->completeItem(kind, id, request.nowMs);
      if (!r.ok) return errorResponse(r);
    } else {
      return notFound();
    }
    Json root = Json::object();
    root.set("ok", Json(true));
    return json(200, root);
  }

  if (p.size() == 3 && p[2] == "sessions") {
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    unsigned id = 0;
    Result r = classroom_->createSession(body, request.nowMs, id);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("id", Json(static_cast<int>(id)));
    return json(200, root);
  }

  if (p.size() == 5 && p[2] == "sessions") {
    unsigned id = 0;
    if (!toId(p[3], id)) return json(400, errorBody("BAD_REQUEST", "Invalid session id."));
    if (p[4] == "delete") {
      if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
      Json out;
      Result r = classroom_->requestDeletion(id, request.nowMs, out);
      if (!r.ok) return errorResponse(r);
      return json(200, out);
    }
    Result r = classroom_->sessionAction(id, p[4], request.nowMs);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    return json(200, root);
  }

  if (p.size() == 3 && p[2] == "attendance") {
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    Result r = classroom_->markAttendance(body);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    return json(200, root);
  }

  if (p.size() == 3 && p[2] == "settings") {
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    Result r = classroom_->setSettings(body);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    return json(200, root);
  }

  if (p.size() >= 4 && p[2] == "submissions") {
    unsigned id = 0;
    if (!toId(p[3], id)) return json(400, errorBody("BAD_REQUEST", "Invalid submission id."));
    if (p.size() == 4 && request.method == "GET") {
      Json out;
      Result r = classroom_->submissionDetail(id, out);
      if (!r.ok) return errorResponse(r);
      out.set("ok", Json(true));
      return json(200, out);
    }
    if (p.size() == 5 && p[4] == "review" && request.method == "POST") {
      Result r = classroom_->reviewSubmission(id);
      if (!r.ok) return errorResponse(r);
      Json root = Json::object();
      root.set("ok", Json(true));
      return json(200, root);
    }
  }

  return notFound();
}

Response Api::student(const Request& request, const std::vector<std::string>& p) {
  if (!classroom_) return json(500, errorBody("INTERNAL", "Controller is not initialised."));

  if (p.size() == 3 && p[2] == "join") {
    if (request.method != "POST") return json(405, errorBody("BAD_REQUEST", "Use POST."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    unsigned studentId = 0, sessionId = 0;
    std::string token;
    Result r = classroom_->join(body.str("code"), body.str("studentNumber"), request.nowMs,
                               studentId, sessionId, token);
    if (!r.ok) return errorResponse(r);
    const Student* st = 0;
    for (size_t k = 0; k < classroom_->students().size(); ++k)
      if (classroom_->students()[k].id == studentId) st = &classroom_->students()[k];

    Json root = Json::object();
    root.set("ok", Json(true));
    root.set("studentToken", Json::makeString(token));
    Json me = Json::object();
    me.set("id", Json(static_cast<int>(studentId)));
    if (st) {
      me.set("number", Json::makeString(st->number));
      me.set("name", Json::makeString(st->name));
      me.set("section", Json::makeString(st->section));
    }
    root.set("student", me);
    Json preview;
    if (classroom_->sessionPreview(body.str("code"), request.nowMs, preview).ok) {
      const Json* session = preview.find("session");
      if (session) root.set("session", *session);
    }
    return json(200, root);
  }

  unsigned studentId = 0, sessionId = 0;
  Result auth = classroom_->authorizeStudent(bearer(request.auth), request.nowMs, studentId, sessionId);
  if (!auth.ok) return errorResponse(auth);

  if (p.size() == 3 && p[2] == "state" && request.method == "GET") {
    Json out;
    Result r = classroom_->studentSnapshot(studentId, request.nowMs, out);
    if (!r.ok) return errorResponse(r);
    return json(200, out);
  }

  if (p.size() == 3 && p[2] == "leave" && request.method == "POST") {
    Result r = classroom_->leave(studentId, request.nowMs);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    return json(200, root);
  }

  if (p.size() == 3 && p[2] == "submit" && request.method == "POST") {
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    Json out;
    Result r = classroom_->submit(studentId, body, request.nowMs, out);
    if (!r.ok) return errorResponse(r);
    return json(200, out);
  }

  if (p.size() == 5 && p[2] == "exams" && p[4] == "start" && request.method == "POST") {
    unsigned examId = 0;
    if (!toId(p[3], examId)) return json(400, errorBody("BAD_REQUEST", "Invalid examination id."));
    Json out;
    Result r = classroom_->startExam(studentId, examId, request.nowMs, out);
    if (!r.ok) return errorResponse(r);
    return json(200, out);
  }

  if (p.size() == 5 && p[2] == "attempts" && p[4] == "answers" && request.method == "POST") {
    unsigned examId = 0;
    if (!toId(p[3], examId)) return json(400, errorBody("BAD_REQUEST", "Invalid examination id."));
    Json body;
    Response failure;
    if (!parseBody(request, body, failure)) return failure;
    const Json* answers = body.find("answers");
    Json empty = Json::object();
    Result r = classroom_->saveAnswers(studentId, examId, answers ? *answers : empty,
                                       request.nowMs);
    if (!r.ok) return errorResponse(r);
    Json root = Json::object();
    root.set("ok", Json(true));
    return json(200, root);
  }

  if (p.size() == 4 && p[2] == "submissions" && request.method == "GET") {
    unsigned id = 0;
    if (!toId(p[3], id)) return json(400, errorBody("BAD_REQUEST", "Invalid submission id."));
    Json out;
    Result r = classroom_->ownSubmission(studentId, id, out);
    if (!r.ok) return errorResponse(r);
    out.set("ok", Json(true));
    return json(200, out);
  }

  return notFound();
}

}  // namespace sntch
