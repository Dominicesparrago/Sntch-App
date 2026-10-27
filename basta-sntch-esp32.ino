// =====================================================================
//  SNTCH ESP32  -  local exam server + teacher dashboard + cloud sync
//  The Arduino Uno keeps the TFT / buttons / LEDs (its code is unchanged).
//  This board handles the network side of the SNTCH objective:
//    - creates the "SNTCH-EXAM" Wi-Fi hotspot (works with NO internet)
//    - serves the student exam page and a JSON API for the Android app
//    - serves the teacher dashboard (create/open/close exam, monitor, results)
//    - stores exams, answers and events on its flash (LittleFS)
//    - uploads saved data to the cloud whenever it has internet (optional)
//
//  Libraries to install: ArduinoJson (version 7.x)
//  Board: ESP32 Dev Module.  Everything else ships with the ESP32 core.
//
//  Student page : http://192.168.4.1/
//  Teacher page : http://192.168.4.1/teacher   (HTTP Basic login below)
//
//  API (same JSON shapes can be hosted in the cloud for online mode)
//    GET  /api/status            public: exam id/title/open, client count
//    GET  /api/exam              exam questions WITHOUT answers (exam open)
//    POST /api/join              {examId, studentId, name}
//    POST /api/heartbeat         {studentId}
//    POST /api/event             {studentId, type}      (tab switch etc.)
//    POST /api/submit            {examId, studentId, name, answers:{qid:value}}
//    --- teacher (Basic auth) ---
//    GET  /api/teacher/monitor   live status of students
//    POST /api/teacher/exam      body = exam JSON
//    POST /api/teacher/open | /close | /clear | /pull | /sync
//    GET  /api/teacher/results   CSV download
//    GET  /api/teacher/qr        QR payloads: Wi-Fi join + session link
//    GET  /qr.js                 QR generator script (served offline)
// =====================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>

// ---------------- Settings (EDIT THESE) ----------------
const char* AP_SSID       = "SNTCH-EXAM";
const char* AP_PASS       = "sntch-1234";          // at least 8 characters
const char* TEACHER_USER  = "teacher";
const char* TEACHER_PASS  = "change-this-password";

// Optional internet uplink (leave STA_SSID empty for hotspot only)
const char* STA_SSID      = "";
const char* STA_PASS      = "";

// Optional cloud sync (https URLs; leave empty to disable)
const char* CLOUD_SUBMIT_URL = "";
const char* CLOUD_EXAM_URL   = "";
const char* DEVICE_TOKEN     = "";
const char* CLOUD_CA_CERT    = nullptr;   // set a root CA PEM for real use

#define MAX_STUDENTS 40

// 1 = students must open the session QR link (it carries a random session code)
// 0 = anyone on the hotspot can join by typing the address
#define REQUIRE_SESSION_CODE 0

// Optional serial link to the Uno for LATER (disabled; Uno code untouched).
// ESP32 TX2 -> Uno RX is 3.3V -> 5V (OK). Uno TX -> ESP32 RX needs a divider.
#define SNTCH_LINK_ENABLED 0
#define LINK_RX 16
#define LINK_TX 17

// ---------------- Files ----------------
const char* EXAM_FILE        = "/exam.json";
const char* SUBMISSIONS_FILE = "/submissions.jsonl";
const char* EVENTS_FILE      = "/events.jsonl";
const char* QUEUE_FILE       = "/queue.jsonl";
const char* QUEUE_TMP        = "/queue.tmp";

WebServer server(80);
DNSServer dns;
Preferences prefs;

String examId;
String examTitle;
String examType = "exam";      // "exam" or "activity"
String sessionCode;            // random code carried by the session QR
int examDuration = 0;          // minutes, 0 = no limit
bool examOpen = false;
bool ntpStarted = false;
unsigned long lastSync = 0;

struct Student {
  String id;
  String name;
  uint32_t lastSeen;
  uint32_t joinedAt;
  uint16_t violations;
  bool submitted;
};
std::vector<Student> students;

// =====================================================================
//  Small helpers
// =====================================================================
bool cloudEnabled() { return strlen(CLOUD_SUBMIT_URL) > 0; }

uint32_t nowSec() {
  time_t t = time(nullptr);
  return (t > 1700000000) ? (uint32_t)t : 0;
}

bool validId(const char* s, size_t maxLen) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n == 0 || n > maxLen) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
  }
  return true;
}

String cleanText(const char* s, size_t maxLen) {
  String out;
  if (!s) return out;
  for (size_t i = 0; s[i] && out.length() < maxLen; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 32 && c != 127) out += (char)c;
  }
  out.trim();
  return out;
}

String readFile(const char* path) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

bool appendLine(const char* path, JsonDocument& d) {
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return false;
  if (f.size() > 300000) { f.close(); return false; }   // keep flash from filling up
  serializeJson(d, f);
  f.print('\n');
  f.close();
  return true;
}

int countLines(const char* path) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  int n = 0;
  while (f.available()) { if (f.read() == '\n') n++; }
  f.close();
  return n;
}

String csvField(const String& in) {
  String s = in;
  if (s.length() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@')) s = "'" + s;
  s.replace("\"", "\"\"");
  return "\"" + s + "\"";
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendErr(int code, const char* msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  String o;
  serializeJson(d, o);
  sendJson(code, o);
}

void sendOk() { sendJson(200, "{\"ok\":true}"); }

bool requireTeacher() {
  if (!server.authenticate(TEACHER_USER, TEACHER_PASS)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// =====================================================================
//  Student tracking (in memory, rebuilt from files at boot)
// =====================================================================
int findStudent(const String& id) {
  for (size_t i = 0; i < students.size(); i++) {
    if (students[i].id == id) return (int)i;
  }
  return -1;
}

int addStudent(const String& id, const String& name) {
  int i = findStudent(id);
  if (i >= 0) {
    if (name.length()) students[i].name = name;
    return i;
  }
  if (students.size() >= MAX_STUDENTS) return -1;
  Student s;
  s.id = id;
  s.name = name;
  s.lastSeen = 0;
  s.joinedAt = 0;
  s.violations = 0;
  s.submitted = false;
  students.push_back(s);
  return (int)students.size() - 1;
}

int submittedCount() {
  int n = 0;
  for (size_t i = 0; i < students.size(); i++) if (students[i].submitted) n++;
  return n;
}

void loadExamMeta() {
  examId = "";
  examTitle = "";
  examType = "exam";
  examDuration = 0;
  String s = readFile(EXAM_FILE);
  if (!s.length()) return;
  JsonDocument d;
  if (deserializeJson(d, s)) return;
  examId = String(d["examId"] | "");
  examTitle = String(d["title"] | "");
  examDuration = d["durationMin"] | 0;
  if (strcmp(d["type"] | "exam", "activity") == 0) examType = "activity";
}

void rebuildState() {
  students.clear();
  if (!examId.length()) return;

  File f = LittleFS.open(EVENTS_FILE, FILE_READ);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;
      JsonDocument d;
      if (deserializeJson(d, line)) continue;
      if (examId != String(d["examId"] | "")) continue;
      int i = addStudent(String(d["studentId"] | ""), "");
      if (i >= 0) students[i].violations++;
    }
    f.close();
  }

  f = LittleFS.open(SUBMISSIONS_FILE, FILE_READ);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;
      JsonDocument d;
      if (deserializeJson(d, line)) continue;
      if (examId != String(d["examId"] | "")) continue;
      int i = addStudent(String(d["studentId"] | ""), String(d["name"] | ""));
      if (i >= 0) students[i].submitted = true;
    }
    f.close();
  }
}

// =====================================================================
//  Exam validation / saving / grading
// =====================================================================
bool validateExam(const String& json, String& err) {
  JsonDocument d;
  if (deserializeJson(d, json)) { err = "Not valid JSON"; return false; }
  if (!validId(d["examId"] | "", 20)) {
    err = "examId must be 1-20 letters, numbers, - or _";
    return false;
  }
  if (strlen(d["title"] | "") == 0) { err = "title is required"; return false; }
  const char* sessType = d["type"] | "exam";
  if (strcmp(sessType, "exam") != 0 && strcmp(sessType, "activity") != 0) {
    err = "type must be exam or activity";
    return false;
  }
  JsonArray qs = d["questions"].as<JsonArray>();
  if (qs.isNull() || qs.size() < 1 || qs.size() > 60) {
    err = "questions must have 1-60 items";
    return false;
  }
  for (JsonObject q : qs) {
    if (!validId(q["id"] | "", 20)) {
      err = "each question needs an id (letters, numbers, - or _)";
      return false;
    }
    if (strlen(q["text"] | "") == 0) { err = "each question needs text"; return false; }
    const char* t = q["type"] | "";
    if (strcmp(t, "mcq") == 0) {
      JsonArray c = q["choices"].as<JsonArray>();
      if (c.isNull() || c.size() < 2 || c.size() > 8) {
        err = "mcq questions need 2-8 choices";
        return false;
      }
    } else if (strcmp(t, "text") != 0) {
      err = "question type must be mcq or text";
      return false;
    }
  }
  return true;
}

void saveExam(const String& body) {
  File f = LittleFS.open(EXAM_FILE, FILE_WRITE);
  if (f) { f.print(body); f.close(); }
  loadExamMeta();
  examOpen = false;                 // teacher opens it explicitly
  prefs.putBool("open", false);
  sessionCode = "";
  prefs.putString("code", "");
  rebuildState();
}

void gradeSubmission(JsonDocument& exam, JsonObject answers, int& score, int& total) {
  score = 0;
  total = 0;
  for (JsonObject q : exam["questions"].as<JsonArray>()) {
    if (q["answer"].isNull()) continue;          // not auto-graded
    int pts = q["points"] | 1;
    total += pts;
    String key = String(q["id"] | "");
    String given = String(answers[key] | "");
    String correct = q["answer"].is<const char*>()
                       ? String(q["answer"].as<const char*>())
                       : String(q["answer"].as<int>());
    given.trim();
    correct.trim();
    given.toLowerCase();
    correct.toLowerCase();
    if (given == correct) score += pts;
  }
}

// =====================================================================
//  Cloud sync (only when CLOUD_* settings are filled in)
// =====================================================================
void setupTls(WiFiClientSecure& c) {
  if (CLOUD_CA_CERT) c.setCACert(CLOUD_CA_CERT);
  else c.setInsecure();                // TESTING ONLY - use a CA cert for real exams
}

bool postToCloud(const String& json) {
  WiFiClientSecure client;
  setupTls(client);
  HTTPClient http;
  if (!http.begin(client, CLOUD_SUBMIT_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  if (strlen(DEVICE_TOKEN)) http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  int code = http.POST(json);
  http.end();
  return code >= 200 && code < 300;
}

void syncQueue() {
  if (!cloudEnabled() || WiFi.status() != WL_CONNECTED) return;
  File in = LittleFS.open(QUEUE_FILE, FILE_READ);
  if (!in) return;
  File keep = LittleFS.open(QUEUE_TMP, FILE_WRITE);
  if (!keep) { in.close(); return; }

  int posted = 0;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    if (posted < 5) {                       // a few per round, keeps the web server responsive
      posted++;
      if (postToCloud(line)) continue;      // uploaded: drop from queue
    }
    keep.println(line);                     // failed or not tried yet: keep
  }
  in.close();
  keep.close();
  LittleFS.remove(QUEUE_FILE);
  LittleFS.rename(QUEUE_TMP, QUEUE_FILE);
}

bool downloadExam(String& err) {
  if (strlen(CLOUD_EXAM_URL) == 0) { err = "Cloud exam URL is not set"; return false; }
  if (WiFi.status() != WL_CONNECTED) { err = "SNTCH is not online"; return false; }
  WiFiClientSecure client;
  setupTls(client);
  HTTPClient http;
  if (!http.begin(client, CLOUD_EXAM_URL)) { err = "Bad cloud URL"; return false; }
  if (strlen(DEVICE_TOKEN)) http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  int code = http.GET();
  if (code != 200) {
    http.end();
    err = "Cloud returned " + String(code);
    return false;
  }
  String body = http.getString();
  http.end();
  if (!validateExam(body, err)) return false;
  saveExam(body);
  return true;
}

// =====================================================================
//  Student / app API
// =====================================================================
String makeCode() {
  const char* alphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // no look-alike characters
  int n = strlen(alphabet);
  String c;
  for (int i = 0; i < 6; i++) c += alphabet[random(n)];
  return c;
}

// Escape characters that have a special meaning inside a Wi-Fi QR payload.
String wifiEscape(const char* in) {
  String o;
  for (; *in; in++) {
    char c = *in;
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o += '\\';
    o += c;
  }
  return o;
}

void handleStatus() {
  JsonDocument d;
  d["device"] = "SNTCH";
  d["examId"] = examId;
  d["title"] = examTitle;
  d["type"] = examType;
  d["codeRequired"] = (REQUIRE_SESSION_CODE != 0);
  d["open"] = examOpen && examId.length() > 0;
  d["clients"] = WiFi.softAPgetStationNum();
  d["online"] = (WiFi.status() == WL_CONNECTED);
  String o;
  serializeJson(d, o);
  sendJson(200, o);
}

void handleExam() {
  if (!examOpen) { sendErr(403, "Exam is not open"); return; }
  JsonDocument d;
  if (deserializeJson(d, readFile(EXAM_FILE))) { sendErr(404, "No exam loaded"); return; }
  for (JsonObject q : d["questions"].as<JsonArray>()) q.remove("answer");   // never leak the key
  String o;
  serializeJson(d, o);
  sendJson(200, o);
}

void handleJoin() {
  if (!examOpen) { sendErr(403, "Exam is not open"); return; }
  JsonDocument b;
  if (deserializeJson(b, server.arg("plain"))) { sendErr(400, "Bad request"); return; }
  const char* sid = b["studentId"] | "";
  if (!validId(sid, 20) || examId != String(b["examId"] | "")) {
    sendErr(400, "Invalid ID or exam");
    return;
  }
#if REQUIRE_SESSION_CODE
  if (sessionCode.length() == 0 || sessionCode != String(b["code"] | "")) {
    sendErr(403, "Scan the session QR code to join");
    return;
  }
#endif
  String name = cleanText(b["name"] | "", 40);
  if (!name.length()) { sendErr(400, "Name is required"); return; }

  int i = findStudent(String(sid));
  if (i >= 0 && students[i].submitted) { sendErr(409, "Already submitted"); return; }
  i = addStudent(String(sid), name);
  if (i < 0) { sendErr(503, "Exam room is full"); return; }

  if (students[i].joinedAt == 0) students[i].joinedAt = millis();
  students[i].lastSeen = millis();

  long remaining = -1;
  if (examDuration > 0) {
    remaining = (long)examDuration * 60L - (long)((millis() - students[i].joinedAt) / 1000UL);
    if (remaining <= 0) { sendErr(403, "Time is up"); return; }
  }
  JsonDocument r;
  r["ok"] = true;
  r["remaining"] = remaining;
  String o;
  serializeJson(r, o);
  sendJson(200, o);
}

void handleHeartbeat() {
  JsonDocument b;
  if (deserializeJson(b, server.arg("plain"))) { sendErr(400, "Bad request"); return; }
  int i = findStudent(String(b["studentId"] | ""));
  if (i < 0) { sendErr(404, "Unknown student"); return; }
  students[i].lastSeen = millis();
  sendJson(200, examOpen ? "{\"ok\":true,\"open\":true}" : "{\"ok\":true,\"open\":false}");
}

void handleEvent() {
  JsonDocument b;
  if (deserializeJson(b, server.arg("plain"))) { sendErr(400, "Bad request"); return; }
  int i = findStudent(String(b["studentId"] | ""));
  if (i < 0 || students[i].submitted) { sendErr(404, "Unknown student"); return; }
  String type = cleanText(b["type"] | "", 24);
  if (!type.length()) { sendErr(400, "Bad request"); return; }

  students[i].violations++;
  students[i].lastSeen = millis();

  JsonDocument rec;
  rec["kind"] = "event";
  rec["examId"] = examId;
  rec["studentId"] = students[i].id;
  rec["type"] = type;
  rec["serverTs"] = nowSec();
  rec["uptimeMs"] = millis();
  appendLine(EVENTS_FILE, rec);
  if (cloudEnabled()) appendLine(QUEUE_FILE, rec);
  sendOk();
}

void handleSubmit() {
  if (!examOpen) { sendErr(403, "Exam is closed"); return; }
  JsonDocument b;
  if (deserializeJson(b, server.arg("plain")) || !b["answers"].is<JsonObject>()) {
    sendErr(400, "Bad request");
    return;
  }
  const char* sid = b["studentId"] | "";
  if (!validId(sid, 20) || examId != String(b["examId"] | "")) {
    sendErr(400, "Invalid ID or exam");
    return;
  }
  int i = findStudent(String(sid));
  if (i < 0) { sendErr(403, "Join the exam first"); return; }
  if (students[i].submitted) { sendErr(409, "Already submitted"); return; }
  if (examDuration > 0 && students[i].joinedAt != 0 &&
      (millis() - students[i].joinedAt) / 1000UL > (unsigned long)examDuration * 60UL + 60UL) {
    sendErr(403, "Time is up");
    return;
  }

  JsonDocument exam;
  if (deserializeJson(exam, readFile(EXAM_FILE))) { sendErr(500, "Exam file unreadable"); return; }
  int score = 0, total = 0;
  gradeSubmission(exam, b["answers"].as<JsonObject>(), score, total);

  JsonDocument rec;
  rec["kind"] = "submission";
  rec["submissionId"] = examId + "-" + students[i].id + "-" + String(millis());
  rec["examId"] = examId;
  rec["studentId"] = students[i].id;
  rec["name"] = students[i].name;
  rec["answers"] = b["answers"];
  rec["score"] = score;
  rec["total"] = total;
  rec["violations"] = students[i].violations;
  rec["clientTs"] = b["clientTs"] | 0;
  rec["serverTs"] = nowSec();
  rec["uptimeMs"] = millis();
  rec["source"] = "sntch-local";

  if (!appendLine(SUBMISSIONS_FILE, rec)) { sendErr(500, "Storage full or failed"); return; }
  if (cloudEnabled()) appendLine(QUEUE_FILE, rec);
  students[i].submitted = true;

  JsonDocument r;
  r["ok"] = true;
  if (total > 0 && (exam["showScore"] | false)) {
    r["score"] = score;
    r["total"] = total;
  }
  String o;
  serializeJson(r, o);
  sendJson(200, o);
}

// =====================================================================
//  Teacher API
// =====================================================================
void handleMonitor() {
  if (!requireTeacher()) return;
  JsonDocument d;
  d["examId"] = examId;
  d["title"] = examTitle;
  d["type"] = examType;
  d["code"] = sessionCode;
  d["open"] = examOpen;
  d["apClients"] = WiFi.softAPgetStationNum();
  d["online"] = (WiFi.status() == WL_CONNECTED);
  d["cloud"] = cloudEnabled();
  d["pending"] = countLines(QUEUE_FILE);
  d["submitted"] = submittedCount();
  JsonArray arr = d["students"].to<JsonArray>();
  for (size_t i = 0; i < students.size(); i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = students[i].id;
    o["name"] = students[i].name;
    o["seen"] = students[i].lastSeen ? (long)((millis() - students[i].lastSeen) / 1000UL) : -1L;
    o["viol"] = students[i].violations;
    o["done"] = students[i].submitted;
  }
  String out;
  serializeJson(d, out);
  sendJson(200, out);
}

void handleSaveExam() {
  if (!requireTeacher()) return;
  String body = server.arg("plain");
  if (body.length() == 0 || body.length() > 24000) { sendErr(400, "Exam must be 1 to 24000 bytes"); return; }
  String err;
  if (!validateExam(body, err)) { sendErr(400, err.c_str()); return; }
  saveExam(body);
  sendOk();
}

void handleOpen() {
  if (!requireTeacher()) return;
  if (!examId.length()) { sendErr(400, "Load an exam first"); return; }
  if (!examOpen || sessionCode.length() == 0) {   // new code each time the session is opened
    sessionCode = makeCode();
    prefs.putString("code", sessionCode);
  }
  examOpen = true;
  prefs.putBool("open", true);
  sendOk();
}

void handleClose() {
  if (!requireTeacher()) return;
  examOpen = false;
  prefs.putBool("open", false);
  sendOk();
}

void handleClear() {
  if (!requireTeacher()) return;
  LittleFS.remove(SUBMISSIONS_FILE);
  LittleFS.remove(EVENTS_FILE);
  LittleFS.remove(QUEUE_FILE);
  students.clear();
  sendOk();
}

void handlePull() {
  if (!requireTeacher()) return;
  String err;
  if (!downloadExam(err)) { sendErr(400, err.c_str()); return; }
  sendOk();
}

void handleSyncNow() {
  if (!requireTeacher()) return;
  if (!cloudEnabled()) { sendErr(400, "Cloud sync is not configured"); return; }
  if (WiFi.status() != WL_CONNECTED) { sendErr(400, "SNTCH is not online"); return; }
  syncQueue();
  sendOk();
}

void handleQr() {
  if (!requireTeacher()) return;
  String base = String("http://") + WiFi.softAPIP().toString() + "/";
  JsonDocument d;
  d["ok"] = true;
  d["ssid"] = AP_SSID;
  d["wifi"] = String("WIFI:T:WPA;S:") + wifiEscape(AP_SSID) + ";P:" + wifiEscape(AP_PASS) + ";;";
  d["type"] = examType;
  d["title"] = examTitle;
  d["open"] = examOpen;
  d["needCode"] = (REQUIRE_SESSION_CODE != 0);
  if (examOpen && sessionCode.length()) {
    d["code"] = sessionCode;
    d["session"] = base + "?s=" + sessionCode;
  } else {
    d["code"] = "";
    d["session"] = "";
  }
  String o;
  serializeJson(d, o);
  sendJson(200, o);
}

void handleResults() {
  if (!requireTeacher()) return;
  server.sendHeader("Content-Disposition", "attachment; filename=results.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("examId,studentId,name,score,total,flags,serverTs,answers\r\n");
  File f = LittleFS.open(SUBMISSIONS_FILE, FILE_READ);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;
      JsonDocument d;
      if (deserializeJson(d, line)) continue;
      String answers;
      serializeJson(d["answers"], answers);
      String row = csvField(String(d["examId"] | "")) + ",";
      row += csvField(String(d["studentId"] | "")) + ",";
      row += csvField(String(d["name"] | "")) + ",";
      row += String((int)(d["score"] | 0)) + ",";
      row += String((int)(d["total"] | 0)) + ",";
      row += String((int)(d["violations"] | 0)) + ",";
      row += String((unsigned long)(d["serverTs"] | 0)) + ",";
      row += csvField(answers) + "\r\n";
      server.sendContent(row);
    }
    f.close();
  }
  server.sendContent("");
}

// =====================================================================
//  Web pages (stored in flash)
// =====================================================================
const char STUDENT_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SNTCH Exam</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f4f5f7;color:#111}
main{max-width:640px;margin:0 auto;padding:16px}
.card{background:#fff;border-radius:12px;padding:16px;margin:12px 0;box-shadow:0 1px 3px #0002}
input[type=text]{width:100%;box-sizing:border-box;padding:10px;font-size:16px;margin:6px 0}
label{display:block;padding:8px 0}
button{padding:12px 18px;font-size:16px;border:0;border-radius:8px;background:#1a73e8;color:#fff;width:100%}
button:disabled{opacity:.5}
.err{color:#b00020;min-height:1.2em}
#timer{position:sticky;top:0;background:#fff;padding:8px;text-align:right;font-weight:600}
</style></head><body><main>
<h2 id="title">SNTCH Exam</h2>
<div id="wait" class="card"><p id="waitmsg">Connecting...</p></div>
<div id="login" class="card" hidden>
  <p>Enter your details to begin.</p>
  <input id="sname" type="text" placeholder="Full name" autocomplete="off">
  <input id="sid" type="text" placeholder="Student ID" autocomplete="off">
  <p class="err" id="lerr"></p>
  <button id="startbtn" onclick="start()">Start exam</button>
</div>
<div id="test" hidden>
  <div id="timer"></div>
  <div id="qs"></div>
  <p class="err" id="serr"></p>
  <button id="sbtn" onclick="submit(false)">Submit answers</button>
</div>
<div id="fin" class="card" hidden><h3>Submitted</h3><p id="result"></p></div>
<script>
const $=id=>document.getElementById(id);
let exam=null,me={id:'',name:''},done=false,hb=null,tick=null,lastEv=0;
const sCode=new URLSearchParams(location.search).get('s')||'';
async function api(path,body){
  const o=body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),keepalive:true}:{};
  const r=await fetch(path,o);return r.json();
}
function show(id){for(const s of ['wait','login','test','fin'])$(s).hidden=(s!==id);}
async function boot(){
  try{
    const st=await api('/api/status');
    const isAct=(st.type==='activity');
    $('title').textContent=(isAct?'Activity: ':'Exam: ')+(st.title||'SNTCH');
    $('startbtn').textContent=isAct?'Start activity':'Start exam';
    if(!st.open){
      $('waitmsg').textContent=st.examId?'The session is not open yet. Please wait...':'No session is loaded.';
      show('wait');setTimeout(boot,3000);return;
    }
    exam=await api('/api/exam');
    if(exam.ok===false){$('waitmsg').textContent=exam.error;show('wait');setTimeout(boot,3000);return;}
    show('login');
  }catch(e){
    $('waitmsg').textContent='Cannot reach SNTCH. Check that you are on the SNTCH-EXAM Wi-Fi.';
    show('wait');setTimeout(boot,4000);
  }
}
function render(){
  const f=$('qs');f.innerHTML='';
  exam.questions.forEach((q,i)=>{
    const d=document.createElement('div');d.className='card';
    const p=document.createElement('p');p.textContent=(i+1)+'. '+q.text;d.appendChild(p);
    if(q.type==='mcq'){
      (q.choices||[]).forEach((c,k)=>{
        const l=document.createElement('label');
        const r=document.createElement('input');r.type='radio';r.name=q.id;r.value=String(k);
        l.appendChild(r);l.appendChild(document.createTextNode(' '+c));d.appendChild(l);
      });
    }else{
      const t=document.createElement('input');t.type='text';t.name=q.id;d.appendChild(t);
    }
    f.appendChild(d);
  });
}
function collect(){
  const a={};
  exam.questions.forEach(q=>{
    if(q.type==='mcq'){
      const c=document.querySelector('input[name="'+q.id+'"]:checked');a[q.id]=c?c.value:'';
    }else{
      const t=document.querySelector('input[name="'+q.id+'"]');a[q.id]=t?t.value:'';
    }
  });
  return a;
}
function ev(t){
  if(done||!me.id)return;
  if(Date.now()-lastEv<1500)return;
  lastEv=Date.now();
  api('/api/event',{studentId:me.id,type:t}).catch(()=>{});
}
function watch(){
  document.addEventListener('visibilitychange',()=>{if(document.hidden)ev('tab_hidden');});
  window.addEventListener('blur',()=>ev('window_blur'));
  document.addEventListener('copy',()=>ev('copy'));
  document.addEventListener('paste',()=>ev('paste'));
}
function startTimer(sec){
  if(sec<0)return;
  const endAt=Date.now()+sec*1000;
  tick=setInterval(()=>{
    const left=Math.max(0,Math.round((endAt-Date.now())/1000));
    $('timer').textContent='Time left: '+Math.floor(left/60)+':'+String(left%60).padStart(2,'0');
    if(left<=0){clearInterval(tick);submit(true);}
  },500);
}
async function start(){
  const id=$('sid').value.trim(),nm=$('sname').value.trim();
  if(!/^[A-Za-z0-9_-]{1,20}$/.test(id)||!nm){
    $('lerr').textContent='Enter your name and a valid ID (letters, numbers, - or _).';return;
  }
  try{
    const r=await api('/api/join',{studentId:id,name:nm,examId:exam.examId,code:sCode});
    if(!r.ok){$('lerr').textContent=r.error||'Cannot join';return;}
    me={id:id,name:nm};render();show('test');watch();
    hb=setInterval(()=>api('/api/heartbeat',{studentId:id}).catch(()=>{}),8000);
    startTimer(r.remaining);
  }catch(e){$('lerr').textContent='Could not reach SNTCH. Try again.';}
}
async function submit(auto){
  if(done)return;
  if(!auto&&!confirm('Submit your answers?'))return;
  $('sbtn').disabled=true;
  try{
    const r=await api('/api/submit',{examId:exam.examId,studentId:me.id,name:me.name,answers:collect(),clientTs:Math.floor(Date.now()/1000)});
    if(r.ok){
      done=true;clearInterval(hb);clearInterval(tick);
      $('result').textContent=(r.score!==undefined)?('Score: '+r.score+' / '+r.total):'Your answers were saved.';
      show('fin');
    }else{$('serr').textContent=r.error||'Submit failed';$('sbtn').disabled=false;}
  }catch(e){$('serr').textContent='Could not reach SNTCH. Try again.';$('sbtn').disabled=false;}
}
boot();
</script></main></body></html>)rawliteral";

const char TEACHER_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SNTCH Teacher</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f4f5f7;color:#111}
main{max-width:820px;margin:0 auto;padding:16px}
.card{background:#fff;border-radius:12px;padding:16px;margin:12px 0;box-shadow:0 1px 3px #0002}
button{padding:10px 14px;font-size:15px;border:0;border-radius:8px;background:#1a73e8;color:#fff;margin:4px 4px 4px 0}
button.red{background:#c62828}
textarea{width:100%;box-sizing:border-box;font-family:monospace;font-size:13px}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:6px;border-bottom:1px solid #ddd;font-size:14px}
#msg{min-height:1.3em;color:#555}
.qrrow{display:flex;flex-wrap:wrap;gap:16px}
.qrbox{flex:1;min-width:220px;text-align:center}
.qrbox svg{width:100%;max-width:280px;height:auto;border:1px solid #ddd}
.qrbox h4{margin:4px 0 8px}
.cap{font-size:13px;color:#555;word-break:break-all;min-height:1.2em}
@media print{button,a,#st,#tb,textarea,#msg,.noprint{display:none}}
</style></head><body><main>
<h2>SNTCH Teacher</h2>
<div id="st" class="card">Loading...</div>
<div class="card">
  <button onclick="act('/api/teacher/open')">Open exam</button>
  <button onclick="act('/api/teacher/close')">Close exam</button>
  <button onclick="act('/api/teacher/pull')">Get exam from cloud</button>
  <button onclick="act('/api/teacher/sync')">Sync now</button>
  <button class="red" onclick="act('/api/teacher/clear')">Clear results</button>
  <a href="/api/teacher/results" download><button type="button">Download results (CSV)</button></a>
  <div id="msg"></div>
</div>
<div class="card">
  <h3>QR Codes</h3>
  <div class="qrrow">
    <div class="qrbox"><h4>1. Join the Wi-Fi</h4><div id="qrwifi"></div><div class="cap" id="capwifi"></div></div>
    <div class="qrbox"><h4 id="sesstitle">2. Session</h4><div id="qrsess"></div><div class="cap" id="capsess"></div></div>
  </div>
  <button onclick="loadQr()">Refresh QR codes</button>
  <button onclick="window.print()">Print</button>
  <p class="noprint">Students scan code 1 with the phone camera to join the Wi-Fi, then code 2 to open the session.</p>
</div>
<div class="card"><h3>Students</h3><table id="tb"></table></div>
<div class="card">
  <h3>Create / load exam (JSON)</h3>
  <textarea id="ex" rows="14" placeholder="Paste exam JSON here"></textarea>
  <button onclick="save()">Save exam</button>
  <button onclick="sample()">Insert exam sample</button>
  <button onclick="sampleActivity()">Insert activity sample</button>
  <p>Saving an exam leaves it closed. Press Open exam when students should start.</p>
</div>
<script src="/qr.js"></script>
<script>
const $=id=>document.getElementById(id);
function cell(tr,t){const td=document.createElement('td');td.textContent=t;tr.appendChild(td);}
async function poll(){
  try{
    const r=await fetch('/api/teacher/monitor');const d=await r.json();
    $('st').textContent=(d.type==='activity'?'Activity: ':'Exam: ')+(d.examId||'none')+' ('+(d.title||'-')+') | '+(d.open?'OPEN':'closed')+
      ' | Wi-Fi clients: '+d.apClients+' | Submitted: '+d.submitted+' | Online: '+(d.online?'yes':'no')+
      (d.cloud?(' | Pending sync: '+d.pending):'');
    const tb=$('tb');tb.innerHTML='';
    const h=document.createElement('tr');
    ['ID','Name','Last seen','Flags','Status'].forEach(x=>{const th=document.createElement('th');th.textContent=x;h.appendChild(th);});
    tb.appendChild(h);
    d.students.forEach(s=>{
      const tr=document.createElement('tr');
      cell(tr,s.id);cell(tr,s.name);cell(tr,s.seen<0?'-':s.seen+'s ago');cell(tr,s.viol);cell(tr,s.done?'Submitted':'Taking');
      tb.appendChild(tr);
    });
  }catch(e){$('st').textContent='Connection lost...';}
}
async function act(path){
  if(path.endsWith('clear')&&!confirm('Delete ALL submissions and events?'))return;
  try{
    const r=await fetch(path,{method:'POST'});const d=await r.json();
    $('msg').textContent=d.ok?'Done.':(d.error||'Failed');
  }catch(e){$('msg').textContent='Request failed.';}
  poll();
  loadQr();
}
async function save(){
  try{
    const r=await fetch('/api/teacher/exam',{method:'POST',headers:{'Content-Type':'application/json'},body:$('ex').value});
    const d=await r.json();$('msg').textContent=d.ok?'Exam saved (closed). Press Open exam to start.':(d.error||'Failed');
  }catch(e){$('msg').textContent='Request failed.';}
  poll();
  loadQr();
}
function sample(){
  $('ex').value=JSON.stringify({
    examId:'EX101',type:'exam',title:'Sample Quiz',durationMin:10,showScore:true,
    questions:[
      {id:'q1',type:'mcq',text:'What is 2 + 2?',choices:['3','4','5'],answer:1,points:1},
      {id:'q2',type:'text',text:'Capital of France?',answer:'Paris',points:1},
      {id:'q3',type:'text',text:'Explain in one sentence what an exam server does.'}
    ]
  },null,2);
}
function sampleActivity(){
  $('ex').value=JSON.stringify({
    examId:'ACT01',type:'activity',title:'Class Activity',durationMin:0,showScore:false,
    questions:[
      {id:'a1',type:'text',text:'Write one thing you learned today.'},
      {id:'a2',type:'mcq',text:'How confident are you with this topic?',choices:['Not yet','Getting there','Confident']}
    ]
  },null,2);
}
function draw(el,text){
  el.innerHTML='';
  if(!text||typeof qrcode==='undefined')return;
  const q=qrcode(0,'M');q.addData(text);q.make();
  el.innerHTML=q.createSvgTag({cellSize:4,margin:2,scalable:true});
}
async function loadQr(){
  try{
    const r=await fetch('/api/teacher/qr');const d=await r.json();
    draw($('qrwifi'),d.wifi);
    $('capwifi').textContent='Network: '+d.ssid;
    const label=(d.type==='activity'?'Activity Session':'Exam Session');
    $('sesstitle').textContent='2. '+label+(d.title?': '+d.title:'');
    if(d.session){
      draw($('qrsess'),d.session);
      $('capsess').textContent='Session code: '+d.code+(d.needCode?' (required to join)':'');
    }else{
      $('qrsess').innerHTML='';
      $('capsess').textContent=d.title?'Press Open to generate the session QR code.':'Load an exam or activity first.';
    }
  }catch(e){$('capsess').textContent='Could not load QR codes.';}
}
setInterval(poll,3000);poll();loadQr();
</script></main></body></html>)rawliteral";

// QR generator: qrcode-generator 2.0.4 by Kazuhiko Arase (MIT license), minified.
const char QR_JS[] PROGMEM = R"rawliteral(var qrcode=function(){var t=function(t,r){var e=t,n=g[r],o=null,i=0,a=null,u=[],f={},c=function(t,r){o=function(t){for(var r=new Array(t),e=0;e<t;e+=1){r[e]=new Array(t);for(var n=0;n<t;n+=1)r[e][n]=null}return r}(i=4*e+17),l(0,0),l(i-7,0),l(0,i-7),s(),h(),d(t,r),e>=7&&v(t),null==a&&(a=p(e,n,u)),w(a,r)},l=function(t,r){for(var e=-1;e<=7;e+=1)if(!(t+e<=-1||i<=t+e))for(var n=-1;n<=7;n+=1)r+n<=-1||i<=r+n||(o[t+e][r+n]=0<=e&&e<=6&&(0==n||6==n)||0<=n&&n<=6&&(0==e||6==e)||2<=e&&e<=4&&2<=n&&n<=4)},h=function(){for(var t=8;t<i-8;t+=1)null==o[t][6]&&(o[t][6]=t%2==0);for(var r=8;r<i-8;r+=1)null==o[6][r]&&(o[6][r]=r%2==0)},s=function(){for(var t=B.getPatternPosition(e),r=0;r<t.length;r+=1)for(var n=0;n<t.length;n+=1){var i=t[r],a=t[n];if(null==o[i][a])for(var u=-2;u<=2;u+=1)for(var f=-2;f<=2;f+=1)o[i+u][a+f]=-2==u||2==u||-2==f||2==f||0==u&&0==f}},v=function(t){for(var r=B.getBCHTypeNumber(e),n=0;n<18;n+=1){var a=!t&&1==(r>>n&1);o[Math.floor(n/3)][n%3+i-8-3]=a}for(n=0;n<18;n+=1){a=!t&&1==(r>>n&1);o[n%3+i-8-3][Math.floor(n/3)]=a}},d=function(t,r){for(var e=n<<3|r,a=B.getBCHTypeInfo(e),u=0;u<15;u+=1){var f=!t&&1==(a>>u&1);u<6?o[u][8]=f:u<8?o[u+1][8]=f:o[i-15+u][8]=f}for(u=0;u<15;u+=1){f=!t&&1==(a>>u&1);u<8?o[8][i-u-1]=f:u<9?o[8][15-u-1+1]=f:o[8][15-u-1]=f}o[i-8][8]=!t},w=function(t,r){for(var e=-1,n=i-1,a=7,u=0,f=B.getMaskFunction(r),c=i-1;c>0;c-=2)for(6==c&&(c-=1);;){for(var g=0;g<2;g+=1)if(null==o[n][c-g]){var l=!1;u<t.length&&(l=1==(t[u]>>>a&1)),f(n,c-g)&&(l=!l),o[n][c-g]=l,-1==(a-=1)&&(u+=1,a=7)}if((n+=e)<0||i<=n){n-=e,e=-e;break}}},p=function(t,r,e){for(var n=A.getRSBlocks(t,r),o=b(),i=0;i<e.length;i+=1){var a=e[i];o.put(a.getMode(),4),o.put(a.getLength(),B.getLengthInBits(a.getMode(),t)),a.write(o)}var u=0;for(i=0;i<n.length;i+=1)u+=n[i].dataCount;if(o.getLengthInBits()>8*u)throw"code length overflow. ("+o.getLengthInBits()+">"+8*u+")";for(o.getLengthInBits()+4<=8*u&&o.put(0,4);o.getLengthInBits()%8!=0;)o.putBit(!1);for(;!(o.getLengthInBits()>=8*u||(o.put(236,8),o.getLengthInBits()>=8*u));)o.put(17,8);return function(t,r){for(var e=0,n=0,o=0,i=new Array(r.length),a=new Array(r.length),u=0;u<r.length;u+=1){var f=r[u].dataCount,c=r[u].totalCount-f;n=Math.max(n,f),o=Math.max(o,c),i[u]=new Array(f);for(var g=0;g<i[u].length;g+=1)i[u][g]=255&t.getBuffer()[g+e];e+=f;var l=B.getErrorCorrectPolynomial(c),h=k(i[u],l.getLength()-1).mod(l);for(a[u]=new Array(l.getLength()-1),g=0;g<a[u].length;g+=1){var s=g+h.getLength()-a[u].length;a[u][g]=s>=0?h.getAt(s):0}}var v=0;for(g=0;g<r.length;g+=1)v+=r[g].totalCount;var d=new Array(v),w=0;for(g=0;g<n;g+=1)for(u=0;u<r.length;u+=1)g<i[u].length&&(d[w]=i[u][g],w+=1);for(g=0;g<o;g+=1)for(u=0;u<r.length;u+=1)g<a[u].length&&(d[w]=a[u][g],w+=1);return d}(o,n)};f.addData=function(t,r){var e=null;switch(r=r||"Byte"){case"Numeric":e=M(t);break;case"Alphanumeric":e=x(t);break;case"Byte":e=m(t);break;case"Kanji":e=L(t);break;default:throw"mode:"+r}u.push(e),a=null},f.isDark=function(t,r){if(t<0||i<=t||r<0||i<=r)throw t+","+r;return o[t][r]},f.getModuleCount=function(){return i},f.make=function(){if(e<1){for(var t=1;t<40;t++){for(var r=A.getRSBlocks(t,n),o=b(),i=0;i<u.length;i++){var a=u[i];o.put(a.getMode(),4),o.put(a.getLength(),B.getLengthInBits(a.getMode(),t)),a.write(o)}var g=0;for(i=0;i<r.length;i++)g+=r[i].dataCount;if(o.getLengthInBits()<=8*g)break}e=t}c(!1,function(){for(var t=0,r=0,e=0;e<8;e+=1){c(!0,e);var n=B.getLostPoint(f);(0==e||t>n)&&(t=n,r=e)}return r}())},f.createTableTag=function(t,r){t=t||2;var e="";e+='<table style="',e+=" border-width: 0px; border-style: none;",e+=" border-collapse: collapse;",e+=" padding: 0px; margin: "+(r=void 0===r?4*t:r)+"px;",e+='">',e+="<tbody>";for(var n=0;n<f.getModuleCount();n+=1){e+="<tr>";for(var o=0;o<f.getModuleCount();o+=1)e+='<td style="',e+=" border-width: 0px; border-style: none;",e+=" border-collapse: collapse;",e+=" padding: 0px; margin: 0px;",e+=" width: "+t+"px;",e+=" height: "+t+"px;",e+=" background-color: ",e+=f.isDark(n,o)?"#000000":"#ffffff",e+=";",e+='"/>';e+="</tr>"}return e+="</tbody>",e+="</table>"},f.createSvgTag=function(t,r,e,n){var o={};"object"==typeof arguments[0]&&(t=(o=arguments[0]).cellSize,r=o.margin,e=o.alt,n=o.title),t=t||2,r=void 0===r?4*t:r,(e="string"==typeof e?{text:e}:e||{}).text=e.text||null,e.id=e.text?e.id||"qrcode-description":null,(n="string"==typeof n?{text:n}:n||{}).text=n.text||null,n.id=n.text?n.id||"qrcode-title":null;var i,a,u,c,g=f.getModuleCount()*t+2*r,l="";for(c="l"+t+",0 0,"+t+" -"+t+",0 0,-"+t+"z ",l+='<svg version="1.1" xmlns="http://www.w3.org/2000/svg"',l+=o.scalable?"":' width="'+g+'px" height="'+g+'px"',l+=' viewBox="0 0 '+g+" "+g+'" ',l+=' preserveAspectRatio="xMinYMin meet"',l+=n.text||e.text?' role="img" aria-labelledby="'+y([n.id,e.id].join(" ").trim())+'"':"",l+=">",l+=n.text?'<title id="'+y(n.id)+'">'+y(n.text)+"</title>":"",l+=e.text?'<description id="'+y(e.id)+'">'+y(e.text)+"</description>":"",l+='<rect width="100%" height="100%" fill="white" cx="0" cy="0"/>',l+='<path d="',a=0;a<f.getModuleCount();a+=1)for(u=a*t+r,i=0;i<f.getModuleCount();i+=1)f.isDark(a,i)&&(l+="M"+(i*t+r)+","+u+c);return l+='" stroke="transparent" fill="black"/>',l+="</svg>"},f.createDataURL=function(t,r){t=t||2,r=void 0===r?4*t:r;var e=f.getModuleCount()*t+2*r,n=r,o=e-r;return I(e,e,function(r,e){if(n<=r&&r<o&&n<=e&&e<o){var i=Math.floor((r-n)/t),a=Math.floor((e-n)/t);return f.isDark(a,i)?0:1}return 1})},f.createImgTag=function(t,r,e){t=t||2,r=void 0===r?4*t:r;var n=f.getModuleCount()*t+2*r,o="";return o+="<img",o+=' src="',o+=f.createDataURL(t,r),o+='"',o+=' width="',o+=n,o+='"',o+=' height="',o+=n,o+='"',e&&(o+=' alt="',o+=y(e),o+='"'),o+="/>"};var y=function(t){for(var r="",e=0;e<t.length;e+=1){var n=t.charAt(e);switch(n){case"<":r+="&lt;";break;case">":r+="&gt;";break;case"&":r+="&amp;";break;case'"':r+="&quot;";break;default:r+=n}}return r};return f.createASCII=function(t,r){if((t=t||1)<2)return function(t){t=void 0===t?2:t;var r,e,n,o,i,a=1*f.getModuleCount()+2*t,u=t,c=a-t,g={"██":"█","█ ":"▀"," █":"▄","  ":" "},l={"██":"▀","█ ":"▀"," █":" ","  ":" "},h="";for(r=0;r<a;r+=2){for(n=Math.floor((r-u)/1),o=Math.floor((r+1-u)/1),e=0;e<a;e+=1)i="█",u<=e&&e<c&&u<=r&&r<c&&f.isDark(n,Math.floor((e-u)/1))&&(i=" "),u<=e&&e<c&&u<=r+1&&r+1<c&&f.isDark(o,Math.floor((e-u)/1))?i+=" ":i+="█",h+=t<1&&r+1>=c?l[i]:g[i];h+="\n"}return a%2&&t>0?h.substring(0,h.length-a-1)+Array(a+1).join("▀"):h.substring(0,h.length-1)}(r);t-=1,r=void 0===r?2*t:r;var e,n,o,i,a=f.getModuleCount()*t+2*r,u=r,c=a-r,g=Array(t+1).join("██"),l=Array(t+1).join("  "),h="",s="";for(e=0;e<a;e+=1){for(o=Math.floor((e-u)/t),s="",n=0;n<a;n+=1)i=1,u<=n&&n<c&&u<=e&&e<c&&f.isDark(o,Math.floor((n-u)/t))&&(i=0),s+=i?g:l;for(o=0;o<t;o+=1)h+=s+"\n"}return h.substring(0,h.length-1)},f.renderTo2dContext=function(t,r){r=r||2;for(var e=f.getModuleCount(),n=0;n<e;n++)for(var o=0;o<e;o++)t.fillStyle=f.isDark(n,o)?"black":"white",t.fillRect(o*r,n*r,r,r)},f};t.stringToBytes=(t.stringToBytesFuncs={default:function(t){for(var r=[],e=0;e<t.length;e+=1){var n=t.charCodeAt(e);r.push(255&n)}return r}}).default,t.createStringToBytes=function(t,r){var e=function(){for(var e=S(t),n=function(){var t=e.read();if(-1==t)throw"eof";return t},o=0,i={};;){var a=e.read();if(-1==a)break;var u=n(),f=n()<<8|n();i[String.fromCharCode(a<<8|u)]=f,o+=1}if(o!=r)throw o+" != "+r;return i}(),n="?".charCodeAt(0);return function(t){for(var r=[],o=0;o<t.length;o+=1){var i=t.charCodeAt(o);if(i<128)r.push(i);else{var a=e[t.charAt(o)];"number"==typeof a?(255&a)==a?r.push(a):(r.push(a>>>8),r.push(255&a)):r.push(n)}}return r}};var r,e,n,o,i,a=1,u=2,f=4,c=8,g={L:1,M:0,Q:3,H:2},l=0,h=1,s=2,v=3,d=4,w=5,p=6,y=7,B=(r=[[],[6,18],[6,22],[6,26],[6,30],[6,34],[6,22,38],[6,24,42],[6,26,46],[6,28,50],[6,30,54],[6,32,58],[6,34,62],[6,26,46,66],[6,26,48,70],[6,26,50,74],[6,30,54,78],[6,30,56,82],[6,30,58,86],[6,34,62,90],[6,28,50,72,94],[6,26,50,74,98],[6,30,54,78,102],[6,28,54,80,106],[6,32,58,84,110],[6,30,58,86,114],[6,34,62,90,118],[6,26,50,74,98,122],[6,30,54,78,102,126],[6,26,52,78,104,130],[6,30,56,82,108,134],[6,34,60,86,112,138],[6,30,58,86,114,142],[6,34,62,90,118,146],[6,30,54,78,102,126,150],[6,24,50,76,102,128,154],[6,28,54,80,106,132,158],[6,32,58,84,110,136,162],[6,26,54,82,110,138,166],[6,30,58,86,114,142,170]],e=1335,n=7973,i=function(t){for(var r=0;0!=t;)r+=1,t>>>=1;return r},(o={}).getBCHTypeInfo=function(t){for(var r=t<<10;i(r)-i(e)>=0;)r^=e<<i(r)-i(e);return 21522^(t<<10|r)},o.getBCHTypeNumber=function(t){for(var r=t<<12;i(r)-i(n)>=0;)r^=n<<i(r)-i(n);return t<<12|r},o.getPatternPosition=function(t){return r[t-1]},o.getMaskFunction=function(t){switch(t){case l:return function(t,r){return(t+r)%2==0};case h:return function(t,r){return t%2==0};case s:return function(t,r){return r%3==0};case v:return function(t,r){return(t+r)%3==0};case d:return function(t,r){return(Math.floor(t/2)+Math.floor(r/3))%2==0};case w:return function(t,r){return t*r%2+t*r%3==0};case p:return function(t,r){return(t*r%2+t*r%3)%2==0};case y:return function(t,r){return(t*r%3+(t+r)%2)%2==0};default:throw"bad maskPattern:"+t}},o.getErrorCorrectPolynomial=function(t){for(var r=k([1],0),e=0;e<t;e+=1)r=r.multiply(k([1,C.gexp(e)],0));return r},o.getLengthInBits=function(t,r){if(1<=r&&r<10)switch(t){case a:return 10;case u:return 9;case f:case c:return 8;default:throw"mode:"+t}else if(r<27)switch(t){case a:return 12;case u:return 11;case f:return 16;case c:return 10;default:throw"mode:"+t}else{if(!(r<41))throw"type:"+r;switch(t){case a:return 14;case u:return 13;case f:return 16;case c:return 12;default:throw"mode:"+t}}},o.getLostPoint=function(t){for(var r=t.getModuleCount(),e=0,n=0;n<r;n+=1)for(var o=0;o<r;o+=1){for(var i=0,a=t.isDark(n,o),u=-1;u<=1;u+=1)if(!(n+u<0||r<=n+u))for(var f=-1;f<=1;f+=1)o+f<0||r<=o+f||0==u&&0==f||a==t.isDark(n+u,o+f)&&(i+=1);i>5&&(e+=3+i-5)}for(n=0;n<r-1;n+=1)for(o=0;o<r-1;o+=1){var c=0;t.isDark(n,o)&&(c+=1),t.isDark(n+1,o)&&(c+=1),t.isDark(n,o+1)&&(c+=1),t.isDark(n+1,o+1)&&(c+=1),0!=c&&4!=c||(e+=3)}for(n=0;n<r;n+=1)for(o=0;o<r-6;o+=1)t.isDark(n,o)&&!t.isDark(n,o+1)&&t.isDark(n,o+2)&&t.isDark(n,o+3)&&t.isDark(n,o+4)&&!t.isDark(n,o+5)&&t.isDark(n,o+6)&&(e+=40);for(o=0;o<r;o+=1)for(n=0;n<r-6;n+=1)t.isDark(n,o)&&!t.isDark(n+1,o)&&t.isDark(n+2,o)&&t.isDark(n+3,o)&&t.isDark(n+4,o)&&!t.isDark(n+5,o)&&t.isDark(n+6,o)&&(e+=40);var g=0;for(o=0;o<r;o+=1)for(n=0;n<r;n+=1)t.isDark(n,o)&&(g+=1);return e+=Math.abs(100*g/r/r-50)/5*10},o),C=function(){for(var t=new Array(256),r=new Array(256),e=0;e<8;e+=1)t[e]=1<<e;for(e=8;e<256;e+=1)t[e]=t[e-4]^t[e-5]^t[e-6]^t[e-8];for(e=0;e<255;e+=1)r[t[e]]=e;var n={glog:function(t){if(t<1)throw"glog("+t+")";return r[t]},gexp:function(r){for(;r<0;)r+=255;for(;r>=256;)r-=255;return t[r]}};return n}();function k(t,r){if(void 0===t.length)throw t.length+"/"+r;var e=function(){for(var e=0;e<t.length&&0==t[e];)e+=1;for(var n=new Array(t.length-e+r),o=0;o<t.length-e;o+=1)n[o]=t[o+e];return n}(),n={getAt:function(t){return e[t]},getLength:function(){return e.length},multiply:function(t){for(var r=new Array(n.getLength()+t.getLength()-1),e=0;e<n.getLength();e+=1)for(var o=0;o<t.getLength();o+=1)r[e+o]^=C.gexp(C.glog(n.getAt(e))+C.glog(t.getAt(o)));return k(r,0)},mod:function(t){if(n.getLength()-t.getLength()<0)return n;for(var r=C.glog(n.getAt(0))-C.glog(t.getAt(0)),e=new Array(n.getLength()),o=0;o<n.getLength();o+=1)e[o]=n.getAt(o);for(o=0;o<t.getLength();o+=1)e[o]^=C.gexp(C.glog(t.getAt(o))+r);return k(e,0).mod(t)}};return n}var A=function(){var t=[[1,26,19],[1,26,16],[1,26,13],[1,26,9],[1,44,34],[1,44,28],[1,44,22],[1,44,16],[1,70,55],[1,70,44],[2,35,17],[2,35,13],[1,100,80],[2,50,32],[2,50,24],[4,25,9],[1,134,108],[2,67,43],[2,33,15,2,34,16],[2,33,11,2,34,12],[2,86,68],[4,43,27],[4,43,19],[4,43,15],[2,98,78],[4,49,31],[2,32,14,4,33,15],[4,39,13,1,40,14],[2,121,97],[2,60,38,2,61,39],[4,40,18,2,41,19],[4,40,14,2,41,15],[2,146,116],[3,58,36,2,59,37],[4,36,16,4,37,17],[4,36,12,4,37,13],[2,86,68,2,87,69],[4,69,43,1,70,44],[6,43,19,2,44,20],[6,43,15,2,44,16],[4,101,81],[1,80,50,4,81,51],[4,50,22,4,51,23],[3,36,12,8,37,13],[2,116,92,2,117,93],[6,58,36,2,59,37],[4,46,20,6,47,21],[7,42,14,4,43,15],[4,133,107],[8,59,37,1,60,38],[8,44,20,4,45,21],[12,33,11,4,34,12],[3,145,115,1,146,116],[4,64,40,5,65,41],[11,36,16,5,37,17],[11,36,12,5,37,13],[5,109,87,1,110,88],[5,65,41,5,66,42],[5,54,24,7,55,25],[11,36,12,7,37,13],[5,122,98,1,123,99],[7,73,45,3,74,46],[15,43,19,2,44,20],[3,45,15,13,46,16],[1,135,107,5,136,108],[10,74,46,1,75,47],[1,50,22,15,51,23],[2,42,14,17,43,15],[5,150,120,1,151,121],[9,69,43,4,70,44],[17,50,22,1,51,23],[2,42,14,19,43,15],[3,141,113,4,142,114],[3,70,44,11,71,45],[17,47,21,4,48,22],[9,39,13,16,40,14],[3,135,107,5,136,108],[3,67,41,13,68,42],[15,54,24,5,55,25],[15,43,15,10,44,16],[4,144,116,4,145,117],[17,68,42],[17,50,22,6,51,23],[19,46,16,6,47,17],[2,139,111,7,140,112],[17,74,46],[7,54,24,16,55,25],[34,37,13],[4,151,121,5,152,122],[4,75,47,14,76,48],[11,54,24,14,55,25],[16,45,15,14,46,16],[6,147,117,4,148,118],[6,73,45,14,74,46],[11,54,24,16,55,25],[30,46,16,2,47,17],[8,132,106,4,133,107],[8,75,47,13,76,48],[7,54,24,22,55,25],[22,45,15,13,46,16],[10,142,114,2,143,115],[19,74,46,4,75,47],[28,50,22,6,51,23],[33,46,16,4,47,17],[8,152,122,4,153,123],[22,73,45,3,74,46],[8,53,23,26,54,24],[12,45,15,28,46,16],[3,147,117,10,148,118],[3,73,45,23,74,46],[4,54,24,31,55,25],[11,45,15,31,46,16],[7,146,116,7,147,117],[21,73,45,7,74,46],[1,53,23,37,54,24],[19,45,15,26,46,16],[5,145,115,10,146,116],[19,75,47,10,76,48],[15,54,24,25,55,25],[23,45,15,25,46,16],[13,145,115,3,146,116],[2,74,46,29,75,47],[42,54,24,1,55,25],[23,45,15,28,46,16],[17,145,115],[10,74,46,23,75,47],[10,54,24,35,55,25],[19,45,15,35,46,16],[17,145,115,1,146,116],[14,74,46,21,75,47],[29,54,24,19,55,25],[11,45,15,46,46,16],[13,145,115,6,146,116],[14,74,46,23,75,47],[44,54,24,7,55,25],[59,46,16,1,47,17],[12,151,121,7,152,122],[12,75,47,26,76,48],[39,54,24,14,55,25],[22,45,15,41,46,16],[6,151,121,14,152,122],[6,75,47,34,76,48],[46,54,24,10,55,25],[2,45,15,64,46,16],[17,152,122,4,153,123],[29,74,46,14,75,47],[49,54,24,10,55,25],[24,45,15,46,46,16],[4,152,122,18,153,123],[13,74,46,32,75,47],[48,54,24,14,55,25],[42,45,15,32,46,16],[20,147,117,4,148,118],[40,75,47,7,76,48],[43,54,24,22,55,25],[10,45,15,67,46,16],[19,148,118,6,149,119],[18,75,47,31,76,48],[34,54,24,34,55,25],[20,45,15,61,46,16]],r=function(t,r){var e={};return e.totalCount=t,e.dataCount=r,e},e={};return e.getRSBlocks=function(e,n){var o=function(r,e){switch(e){case g.L:return t[4*(r-1)+0];case g.M:return t[4*(r-1)+1];case g.Q:return t[4*(r-1)+2];case g.H:return t[4*(r-1)+3];default:return}}(e,n);if(void 0===o)throw"bad rs block @ typeNumber:"+e+"/errorCorrectionLevel:"+n;for(var i=o.length/3,a=[],u=0;u<i;u+=1)for(var f=o[3*u+0],c=o[3*u+1],l=o[3*u+2],h=0;h<f;h+=1)a.push(r(c,l));return a},e}(),b=function(){var t=[],r=0,e={getBuffer:function(){return t},getAt:function(r){var e=Math.floor(r/8);return 1==(t[e]>>>7-r%8&1)},put:function(t,r){for(var n=0;n<r;n+=1)e.putBit(1==(t>>>r-n-1&1))},getLengthInBits:function(){return r},putBit:function(e){var n=Math.floor(r/8);t.length<=n&&t.push(0),e&&(t[n]|=128>>>r%8),r+=1}};return e},M=function(t){var r=a,e=t,n={getMode:function(){return r},getLength:function(t){return e.length},write:function(t){for(var r=e,n=0;n+2<r.length;)t.put(o(r.substring(n,n+3)),10),n+=3;n<r.length&&(r.length-n==1?t.put(o(r.substring(n,n+1)),4):r.length-n==2&&t.put(o(r.substring(n,n+2)),7))}},o=function(t){for(var r=0,e=0;e<t.length;e+=1)r=10*r+i(t.charAt(e));return r},i=function(t){if("0"<=t&&t<="9")return t.charCodeAt(0)-"0".charCodeAt(0);throw"illegal char :"+t};return n},x=function(t){var r=u,e=t,n={getMode:function(){return r},getLength:function(t){return e.length},write:function(t){for(var r=e,n=0;n+1<r.length;)t.put(45*o(r.charAt(n))+o(r.charAt(n+1)),11),n+=2;n<r.length&&t.put(o(r.charAt(n)),6)}},o=function(t){if("0"<=t&&t<="9")return t.charCodeAt(0)-"0".charCodeAt(0);if("A"<=t&&t<="Z")return t.charCodeAt(0)-"A".charCodeAt(0)+10;switch(t){case" ":return 36;case"$":return 37;case"%":return 38;case"*":return 39;case"+":return 40;case"-":return 41;case".":return 42;case"/":return 43;case":":return 44;default:throw"illegal char :"+t}};return n},m=function(r){var e=f,n=t.stringToBytes(r),o={getMode:function(){return e},getLength:function(t){return n.length},write:function(t){for(var r=0;r<n.length;r+=1)t.put(n[r],8)}};return o},L=function(r){var e=c,n=t.stringToBytesFuncs.SJIS;if(!n)throw"sjis not supported.";!function(){var t=n("友");if(2!=t.length||38726!=(t[0]<<8|t[1]))throw"sjis not supported."}();var o=n(r),i={getMode:function(){return e},getLength:function(t){return~~(o.length/2)},write:function(t){for(var r=o,e=0;e+1<r.length;){var n=(255&r[e])<<8|255&r[e+1];if(33088<=n&&n<=40956)n-=33088;else{if(!(57408<=n&&n<=60351))throw"illegal char at "+(e+1)+"/"+n;n-=49472}n=192*(n>>>8&255)+(255&n),t.put(n,13),e+=2}if(e<r.length)throw"illegal char at "+(e+1)}};return i},D=function(){var t=[],r={writeByte:function(r){t.push(255&r)},writeShort:function(t){r.writeByte(t),r.writeByte(t>>>8)},writeBytes:function(t,e,n){e=e||0,n=n||t.length;for(var o=0;o<n;o+=1)r.writeByte(t[o+e])},writeString:function(t){for(var e=0;e<t.length;e+=1)r.writeByte(t.charCodeAt(e))},toByteArray:function(){return t},toString:function(){var r="";r+="[";for(var e=0;e<t.length;e+=1)e>0&&(r+=","),r+=t[e];return r+="]"}};return r},S=function(t){var r=t,e=0,n=0,o=0,i={read:function(){for(;o<8;){if(e>=r.length){if(0==o)return-1;throw"unexpected end of file./"+o}var t=r.charAt(e);if(e+=1,"="==t)return o=0,-1;t.match(/^\s$/)||(n=n<<6|a(t.charCodeAt(0)),o+=6)}var i=n>>>o-8&255;return o-=8,i}},a=function(t){if(65<=t&&t<=90)return t-65;if(97<=t&&t<=122)return t-97+26;if(48<=t&&t<=57)return t-48+52;if(43==t)return 62;if(47==t)return 63;throw"c:"+t};return i},I=function(t,r,e){for(var n=function(t,r){var e=t,n=r,o=new Array(t*r),i={setPixel:function(t,r,n){o[r*e+t]=n},write:function(t){t.writeString("GIF87a"),t.writeShort(e),t.writeShort(n),t.writeByte(128),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(255),t.writeByte(255),t.writeByte(255),t.writeString(","),t.writeShort(0),t.writeShort(0),t.writeShort(e),t.writeShort(n),t.writeByte(0);var r=a(2);t.writeByte(2);for(var o=0;r.length-o>255;)t.writeByte(255),t.writeBytes(r,o,255),o+=255;t.writeByte(r.length-o),t.writeBytes(r,o,r.length-o),t.writeByte(0),t.writeString(";")}},a=function(t){for(var r=1<<t,e=1+(1<<t),n=t+1,i=u(),a=0;a<r;a+=1)i.add(String.fromCharCode(a));i.add(String.fromCharCode(r)),i.add(String.fromCharCode(e));var f,c,g,l=D(),h=(f=l,c=0,g=0,{write:function(t,r){if(t>>>r!=0)throw"length over";for(;c+r>=8;)f.writeByte(255&(t<<c|g)),r-=8-c,t>>>=8-c,g=0,c=0;g|=t<<c,c+=r},flush:function(){c>0&&f.writeByte(g)}});h.write(r,n);var s=0,v=String.fromCharCode(o[s]);for(s+=1;s<o.length;){var d=String.fromCharCode(o[s]);s+=1,i.contains(v+d)?v+=d:(h.write(i.indexOf(v),n),i.size()<4095&&(i.size()==1<<n&&(n+=1),i.add(v+d)),v=d)}return h.write(i.indexOf(v),n),h.write(e,n),h.flush(),l.toByteArray()},u=function(){var t={},r=0,e={add:function(n){if(e.contains(n))throw"dup key:"+n;t[n]=r,r+=1},size:function(){return r},indexOf:function(r){return t[r]},contains:function(r){return void 0!==t[r]}};return e};return i}(t,r),o=0;o<r;o+=1)for(var i=0;i<t;i+=1)n.setPixel(i,o,e(i,o));var a=D();n.write(a);for(var u=function(){var t=0,r=0,e=0,n="",o={},i=function(t){n+=String.fromCharCode(a(63&t))},a=function(t){if(t<0);else{if(t<26)return 65+t;if(t<52)return t-26+97;if(t<62)return t-52+48;if(62==t)return 43;if(63==t)return 47}throw"n:"+t};return o.writeByte=function(n){for(t=t<<8|255&n,r+=8,e+=1;r>=6;)i(t>>>r-6),r-=6},o.flush=function(){if(r>0&&(i(t<<6-r),t=0,r=0),e%3!=0)for(var o=3-e%3,a=0;a<o;a+=1)n+="="},o.toString=function(){return n},o}(),f=a.toByteArray(),c=0;c<f.length;c+=1)u.writeByte(f[c]);return u.flush(),"data:image/gif;base64,"+u};return t}();qrcode.stringToBytesFuncs["UTF-8"]=function(t){return function(t){for(var r=[],e=0;e<t.length;e++){var n=t.charCodeAt(e);n<128?r.push(n):n<2048?r.push(192|n>>6,128|63&n):n<55296||n>=57344?r.push(224|n>>12,128|n>>6&63,128|63&n):(e++,n=65536+((1023&n)<<10|1023&t.charCodeAt(e)),r.push(240|n>>18,128|n>>12&63,128|n>>6&63,128|63&n))}return r}(t)},function(t){"function"==typeof define&&define.amd?define([],t):"object"==typeof exports&&(module.exports=t())}(function(){return qrcode});)rawliteral";

void handleQrJs() {
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "application/javascript", QR_JS);
}

void handleStudentPage() { server.send_P(200, "text/html", STUDENT_HTML); }
void handleTeacherPage() {
  if (!requireTeacher()) return;
  server.send_P(200, "text/html", TEACHER_HTML);
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) { sendErr(404, "Not found"); return; }
  // Captive-portal style redirect so phones open the exam page.
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// =====================================================================
//  Optional status line for the Uno (disabled by default)
// =====================================================================
#if SNTCH_LINK_ENABLED
void linkStatus() {
  static unsigned long last = 0;
  if (millis() - last < 2000) return;
  last = millis();
  Serial2.printf("STAT open=%d clients=%d submitted=%d\n",
                 examOpen ? 1 : 0, WiFi.softAPgetStationNum(), submittedCount());
}
#endif

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  if (!LittleFS.begin(true)) Serial.println(F("LittleFS mount failed"));
  prefs.begin("sntch", false);
  examOpen = prefs.getBool("open", false);
  sessionCode = prefs.getString("code", "");
  loadExamMeta();
  if (!examId.length()) examOpen = false;
  if (!examOpen) sessionCode = "";
  rebuildState();

  bool wantSta = strlen(STA_SSID) > 0;
  WiFi.mode(wantSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 8);
  if (wantSta) WiFi.begin(STA_SSID, STA_PASS);
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleStudentPage);
  server.on("/teacher", HTTP_GET, handleTeacherPage);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/exam", HTTP_GET, handleExam);
  server.on("/api/join", HTTP_POST, handleJoin);
  server.on("/api/heartbeat", HTTP_POST, handleHeartbeat);
  server.on("/api/event", HTTP_POST, handleEvent);
  server.on("/api/submit", HTTP_POST, handleSubmit);
  server.on("/api/teacher/monitor", HTTP_GET, handleMonitor);
  server.on("/api/teacher/exam", HTTP_POST, handleSaveExam);
  server.on("/api/teacher/open", HTTP_POST, handleOpen);
  server.on("/api/teacher/close", HTTP_POST, handleClose);
  server.on("/api/teacher/clear", HTTP_POST, handleClear);
  server.on("/api/teacher/pull", HTTP_POST, handlePull);
  server.on("/api/teacher/sync", HTTP_POST, handleSyncNow);
  server.on("/api/teacher/results", HTTP_GET, handleResults);
  server.on("/api/teacher/qr", HTTP_GET, handleQr);
  server.on("/qr.js", HTTP_GET, handleQrJs);
  server.onNotFound(handleNotFound);
  server.begin();

#if SNTCH_LINK_ENABLED
  Serial2.begin(9600, SERIAL_8N1, LINK_RX, LINK_TX);
#endif

  Serial.println();
  Serial.println(F("SNTCH ESP32 ready"));
  Serial.print(F("Hotspot : ")); Serial.println(AP_SSID);
  Serial.print(F("Student : http://")); Serial.println(WiFi.softAPIP());
  Serial.print(F("Teacher : http://")); Serial.print(WiFi.softAPIP()); Serial.println(F("/teacher"));
}

void loop() {
  dns.processNextRequest();
  server.handleClient();

  if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org");
    ntpStarted = true;
    Serial.println(F("Online: clock sync started"));
  }

  if (cloudEnabled() && WiFi.status() == WL_CONNECTED && millis() - lastSync > 30000UL) {
    lastSync = millis();
    syncQueue();
  }

#if SNTCH_LINK_ENABLED
  linkStatus();
#endif
}
