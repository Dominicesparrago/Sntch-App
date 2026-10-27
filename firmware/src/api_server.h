#pragma once

#include <WebServer.h>

#include "sntch/api.hpp"
#include "sntch/classroom.hpp"

namespace sntch_fw {

// Arduino WebServer::begin() returns void. Its NetworkServer member is
// protected and exposes operator bool() for the actual bind/listen state.
class ListeningWebServer : public WebServer {
 public:
  explicit ListeningWebServer(uint16_t port) : WebServer(port) {}
  bool isListening() { return static_cast<bool>(_server); }
};

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
  ListeningWebServer server_;
  bool routesRegistered_ = false;
  bool listening_ = false;
};

}  // namespace sntch_fw
