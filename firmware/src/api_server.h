#pragma once

#include <WebServer.h>

#include "sntch/api.hpp"
#include "sntch/classroom.hpp"

namespace sntch_fw {

// Bridges the Arduino WebServer to the transport-neutral core Api. No protocol
// logic lives here; this only converts HTTP into Request and back.
class ApiServer {
 public:
  ApiServer(sntch::Api* api, sntch::Classroom* classroom);

  bool begin();
  void handle();

 private:
  void dispatch();
  sntch::Api* api_;
  sntch::Classroom* classroom_;
  WebServer server_;
};

}  // namespace sntch_fw
