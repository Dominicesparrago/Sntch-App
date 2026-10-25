// Transport-neutral HTTP/JSON router. The ESP32 WebServer glue and the host
// simulator both feed Request objects here, so there is exactly one
// implementation of the protocol.
#pragma once

#include <string>
#include <vector>

#include "sntch/classroom.hpp"
#include "sntch/json.hpp"

namespace sntch {

struct Request {
  std::string method;
  std::string path;   // without query string, e.g. /api/teacher/state
  std::string body;
  std::string auth;   // Authorization header value, e.g. "Bearer abc"
  std::string ip;     // client address, for rate limiting and logs
  Ms nowMs;

  Request() : nowMs(0) {}
};

struct Response {
  int status;
  std::string body;
  std::string contentType;

  Response() : status(200), contentType("application/json") {}
};

class Api {
 public:
  Api(Classroom* classroom, const DeviceInfo& device);

  void setDeviceInfo(const DeviceInfo& device) { device_ = device; }
  Response handle(const Request& request);

  // Test/diagnostic helpers.
  void resetRateLimits() { windows_.clear(); }

 private:
  Classroom* classroom_;
  DeviceInfo device_;

  struct Window {
    std::string ip;
    Ms startMs;
    int count;
    Window() : startMs(0), count(0) {}
  };
  std::vector<Window> windows_;

  bool rateLimited(const Request& request);

  Response dispatch(const Request& request, const std::vector<std::string>& parts, bool& handled);
  Response json(int status, const Json& body);
  Response errorResponse(const Result& result);
  Response notFound();

  std::string bearer(const std::string& authHeader) const;
  Response teacher(const Request& request, const std::vector<std::string>& parts);
  Response student(const Request& request, const std::vector<std::string>& parts);
  Response publicRoute(const Request& request, const std::vector<std::string>& parts);
};

}  // namespace sntch
