#include "api_server.h"

#include <Arduino.h>

#include "sntch_config.h"
#include "uptime.h"

namespace sntch_fw {

namespace {
const char* methodName(HTTPMethod method) {
  switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    default: return "GET";
  }
}
}  // namespace

ApiServer::ApiServer(sntch::Api* api, sntch::Classroom* classroom)
    : api_(api), classroom_(classroom), server_(SNTCH_HTTP_PORT) {}

bool ApiServer::begin() {
  if (listening_) return true;
  if (!api_ || !classroom_) return false;

  if (!routesRegistered_) {
    static const char* kHeaders[] = {"Authorization"};
    server_.collectHeaders(kHeaders, 1);
    server_.on("/", HTTP_GET, [this]() {
      server_.send(200, "text/plain",
                   String("SNTCH controller ") + SNTCH_FIRMWARE_VERSION +
                       "\nUse the SNTCH Android app on this network.\n/api/health reports status.\n");
    });
    server_.onNotFound([this]() { dispatch(); });
    routesRegistered_ = true;
  }

  // WebServer::begin() returns void; inspect the underlying listener state.
  server_.begin();
  listening_ = server_.isListening();
  if (!listening_) {
    server_.stop();
    Serial.printf("[sntch] HTTP API listener failed on port %u\n", SNTCH_HTTP_PORT);
    return false;
  }

  Serial.printf("[sntch] HTTP API listening on %u\n", SNTCH_HTTP_PORT);
  return true;
}

void ApiServer::handle() { server_.handleClient(); }

void ApiServer::dispatch() {
  sntch::Request request;
  request.method = methodName(server_.method());
  request.path = server_.uri().c_str();
  request.body = server_.arg("plain").c_str();
  request.auth = server_.header("Authorization").c_str();
  request.ip = server_.client().remoteIP().toString().c_str();
  request.nowMs = uptimeMs();

  sntch::Response response = api_->handle(request);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(response.status, response.contentType.c_str(), response.body.c_str());
}

}  // namespace sntch_fw
