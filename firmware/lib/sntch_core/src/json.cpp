#include "sntch/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sntch {
namespace {

const int kMaxDepth = 8;
const size_t kMaxNodes = 2048;

struct Parser {
  const std::string& in;
  size_t i;
  std::string err;
  size_t nodes;
  int depth;

  explicit Parser(const std::string& s) : in(s), i(0), nodes(0), depth(0) {}

  void ws() {
    while (i < in.size()) {
      char c = in[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i;
      } else {
        break;
      }
    }
  }

  bool fail(const char* message) {
    if (err.empty()) err = message;
    return false;
  }

  bool node() {
    if (++nodes > kMaxNodes) return fail("document too complex");
    return true;
  }

  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool hex4(unsigned& out) {
    if (i + 4 > in.size()) return fail("bad unicode escape");
    unsigned v = 0;
    for (int k = 0; k < 4; ++k) {
      char c = in[i + k];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else return fail("bad unicode escape");
    }
    i += 4;
    out = v;
    return true;
  }

  bool string(std::string& out) {
    if (i >= in.size() || in[i] != '"') return fail("expected string");
    ++i;
    out.clear();
    while (i < in.size()) {
      unsigned char c = static_cast<unsigned char>(in[i++]);
      if (c == '"') return true;
      if (c == '\\') {
        if (i >= in.size()) return fail("bad escape");
        char e = in[i++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            unsigned cp = 0;
            if (!hex4(cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size() && in[i] == '\\' && in[i + 1] == 'u') {
              i += 2;
              unsigned lo = 0;
              if (!hex4(lo)) return false;
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            appendUtf8(out, cp);
            break;
          }
          default: return fail("bad escape");
        }
      } else if (c < 0x20) {
        return fail("control character in string");
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
    return fail("unterminated string");
  }

  bool value(Json& out) {
    if (!node()) return false;
    ws();
    if (i >= in.size()) return fail("unexpected end of input");
    char c = in[i];
    if (c == '{') {
      if (depth + 1 > kMaxDepth) return fail("document too deep");
      ++depth;
      out = Json::object();
      ++i;
      ws();
      if (i < in.size() && in[i] == '}') { ++i; --depth; return true; }
      for (;;) {
        ws();
        std::string key;
        if (!string(key)) return false;
        ws();
        if (i >= in.size() || in[i] != ':') return fail("expected ':'");
        ++i;
        Json child;
        if (!value(child)) return false;
        out.set(key, child);
        ws();
        if (i < in.size() && in[i] == ',') { ++i; continue; }
        if (i < in.size() && in[i] == '}') { ++i; --depth; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      if (depth + 1 > kMaxDepth) return fail("document too deep");
      ++depth;
      out = Json::array();
      ++i;
      ws();
      if (i < in.size() && in[i] == ']') { ++i; --depth; return true; }
      for (;;) {
        Json child;
        if (!value(child)) return false;
        out.push(child);
        ws();
        if (i < in.size() && in[i] == ',') { ++i; continue; }
        if (i < in.size() && in[i] == ']') { ++i; --depth; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out = Json(std::string());
      return string(out.text);
    }
    if (c == 't' && in.compare(i, 4, "true") == 0) { i += 4; out = Json(true); return true; }
    if (c == 'f' && in.compare(i, 5, "false") == 0) { i += 5; out = Json(false); return true; }
    if (c == 'n' && in.compare(i, 4, "null") == 0) { i += 4; out = Json(); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
      size_t start = i;
      if (in[i] == '-') ++i;
      bool digits = false;
      while (i < in.size() && in[i] >= '0' && in[i] <= '9') { ++i; digits = true; }
      if (i < in.size() && in[i] == '.') {
        ++i;
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') { ++i; digits = true; }
      }
      if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
        ++i;
        if (i < in.size() && (in[i] == '+' || in[i] == '-')) ++i;
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') ++i;
      }
      if (!digits) return fail("bad number");
      out = Json(std::strtod(in.substr(start, i - start).c_str(), 0));
      return true;
    }
    return fail("unexpected token");
  }
};

void dumpString(const std::string& s, std::string& out) {
  out.push_back('"');
  out += jsonEscape(s);
  out.push_back('"');
}

void dumpValue(const Json& v, std::string& out) {
  switch (v.type) {
    case Json::NUL: out += "null"; break;
    case Json::BOOL: out += v.boolean ? "true" : "false"; break;
    case Json::NUM: {
      char buf[32];
      double d = v.number;
      if (d == static_cast<double>(static_cast<long long>(d))) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
      } else {
        std::snprintf(buf, sizeof(buf), "%.6f", d);
      }
      out += buf;
      break;
    }
    case Json::STR: dumpString(v.text, out); break;
    case Json::ARR:
      out.push_back('[');
      for (size_t k = 0; k < v.items.size(); ++k) {
        if (k) out.push_back(',');
        dumpValue(v.items[k], out);
      }
      out.push_back(']');
      break;
    case Json::OBJ:
      out.push_back('{');
      for (size_t k = 0; k < v.members.size(); ++k) {
        if (k) out.push_back(',');
        dumpString(v.members[k].first, out);
        out.push_back(':');
        dumpValue(v.members[k].second, out);
      }
      out.push_back('}');
      break;
  }
}

}  // namespace

Json Json::array() {
  Json j;
  j.type = ARR;
  return j;
}

Json Json::object() {
  Json j;
  j.type = OBJ;
  return j;
}

const Json* Json::find(const char* key) const {
  if (type != OBJ || !key) return 0;
  for (size_t k = 0; k < members.size(); ++k) {
    if (members[k].first == key) return &members[k].second;
  }
  return 0;
}

std::string Json::str(const char* key, const std::string& fallback) const {
  const Json* v = find(key);
  return (v && v->type == STR) ? v->text : fallback;
}

double Json::num(const char* key, double fallback) const {
  const Json* v = find(key);
  return (v && v->type == NUM) ? v->number : fallback;
}

bool Json::flag(const char* key, bool fallback) const {
  const Json* v = find(key);
  return (v && v->type == BOOL) ? v->boolean : fallback;
}

int Json::integer(const char* key, int fallback) const {
  const Json* v = find(key);
  if (!v || v->type != NUM) return fallback;
  return static_cast<int>(v->number);
}

Json& Json::set(const std::string& key, const Json& value) {
  if (type != OBJ) {
    type = OBJ;
    items.clear();
  }
  for (size_t k = 0; k < members.size(); ++k) {
    if (members[k].first == key) {
      members[k].second = value;
      return *this;
    }
  }
  members.push_back(std::make_pair(key, value));
  return *this;
}

Json& Json::push(const Json& value) {
  if (type != ARR) {
    type = ARR;
    members.clear();
  }
  items.push_back(value);
  return *this;
}

std::string Json::dump() const {
  std::string out;
  out.reserve(256);
  dumpValue(*this, out);
  return out;
}

std::string jsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (size_t k = 0; k < in.size(); ++k) {
    unsigned char c = static_cast<unsigned char>(in[k]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

bool parseJson(const std::string& input, Json& out, std::string* error) {
  Parser p(input);
  p.ws();
  if (p.i >= input.size()) {
    if (error) *error = "empty document";
    return false;
  }
  Json v;
  if (!p.value(v)) {
    if (error) *error = p.err;
    return false;
  }
  p.ws();
  if (p.i != input.size()) {
    if (error) *error = "trailing characters";
    return false;
  }
  out = v;
  return true;
}

}  // namespace sntch
