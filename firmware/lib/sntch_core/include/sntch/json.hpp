// Minimal dependency-free JSON document type and parser used by the SNTCH core.
// Bounded by design: fixed depth and node budget so a malformed request cannot
// exhaust the ESP32 heap.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sntch {

class Json {
 public:
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };

  Json() : type(NUL), boolean(false), number(0) {}
  explicit Json(bool v) : type(BOOL), boolean(v), number(0) {}
  explicit Json(int v) : type(NUM), boolean(false), number(v) {}
  explicit Json(double v) : type(NUM), boolean(false), number(v) {}
  explicit Json(const std::string& v) : type(STR), boolean(false), number(0), text(v) {}

  static Json array();
  static Json object();

  Type type;
  bool boolean;
  double number;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json> > members;

  bool isNull() const { return type == NUL; }
  bool isArray() const { return type == ARR; }
  bool isObject() const { return type == OBJ; }
  bool isString() const { return type == STR; }
  bool isNumber() const { return type == NUM; }
  bool isBool() const { return type == BOOL; }

  const Json* find(const char* key) const;
  bool has(const char* key) const { return find(key) != 0; }
  std::string str(const char* key, const std::string& fallback = std::string()) const;
  double num(const char* key, double fallback = 0) const;
  bool flag(const char* key, bool fallback = false) const;
  int integer(const char* key, int fallback = 0) const;

  Json& set(const std::string& key, const Json& value);
  Json& push(const Json& value);

  std::string dump() const;

  // Convenience builders (named distinctly so they do not overload the accessors).
  static Json makeString(const char* v) { return Json(std::string(v ? v : "")); }
  static Json makeString(const std::string& v) { return Json(v); }
  static Json makeNumber(double v) { return Json(v); }
};

// Parses `input`. Returns false and fills `error` on malformed input.
bool parseJson(const std::string& input, Json& out, std::string* error = 0);

std::string jsonEscape(const std::string& in);

}  // namespace sntch
