// SNTCH controller firmware for ESP32-WROOM-32D (classic ESP32).
//
// Boot: mount LittleFS, load the classroom snapshot, start the SNTCH Wi-Fi
// access point, serve the local HTTP/JSON API. All classroom logic lives in
// lib/sntch_core, which is also compiled and tested on the host.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "access_point.h"
#include "api_server.h"
#include "littlefs_storage.h"
#include "network_recovery.h"
#include "sntch/api.hpp"
#include "sntch/classroom.hpp"
#include "sntch_config.h"
#include "uptime.h"

// Keep Arduino's sketch preprocessor from generating setup()/loop() prototypes
// inside the private namespace below.
void setup();
void loop();

using sntch::Classroom;
using sntch::DeviceInfo;

namespace {

sntch_fw::LittleFsStorage g_storage;
Classroom g_classroom(&g_storage, SNTCH_TEACHER_PIN);
sntch_fw::AccessPoint g_ap;
DeviceInfo g_device;
sntch::Api g_api(&g_classroom, g_device);
sntch_fw::ApiServer g_server(&g_api, &g_classroom);

uint32_t g_lastSaveMs = 0;
uint32_t g_lastDiagMs = 0;
#if SNTCH_UNO_STATUS
uint32_t g_lastUnoStatusMs = 0;
#endif
int g_lastClients = -1;
sntch_fw::NetworkRecovery g_network;
bool g_hasReportedReady = false;
bool g_classroomReady = false;
constexpr uint32_t kApRetryIntervalMs = 5000;

String deviceName() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "SNTCH-%02X%02X", mac[4], mac[5]);
  return String(buffer);
}

DeviceInfo collectDeviceInfo(const String& name) {
  DeviceInfo info;
  info.name = name.c_str();
  info.ssid = g_ap.ssid().c_str();
  info.ip = g_ap.ip().c_str();
  info.firmware = SNTCH_FIRMWARE_VERSION;
  info.freeHeap = static_cast<size_t>(ESP.getFreeHeap());
  info.storageUsed = g_storage.usedBytes();
  info.storageTotal = g_storage.totalBytes();
  return info;
}

void logDiagnostics(const String& name) {
  int clients = g_ap.clientCount();
  Serial.printf("[sntch] up=%lus heap=%uB clients=%d students=%u sessions=%u subs=%u storage=%u/%u%s\n",
                static_cast<unsigned long>(sntch_fw::uptimeMs() / 1000),
                static_cast<unsigned>(ESP.getFreeHeap()), clients,
                static_cast<unsigned>(g_classroom.studentCount()),
                static_cast<unsigned>(g_classroom.openSessionCount()),
                static_cast<unsigned>(g_classroom.submissionCount()),
                static_cast<unsigned>(g_storage.usedBytes()),
                static_cast<unsigned>(g_storage.totalBytes()),
                g_classroom.storageError() ? " STORAGE-ERROR" : "");
  if (clients != g_lastClients) {
    Serial.printf("[sntch] wifi clients now %d (limit %d)\n", clients, SNTCH_AP_MAX_CLIENTS);
    g_lastClients = clients;
  }
  (void)name;
}

void refreshDeviceInfo() {
  const String name = deviceName();
  g_device = collectDeviceInfo(name);
  g_api.setDeviceInfo(g_device);
}

#if SNTCH_UNO_STATUS
uint8_t statusChecksum(const std::string& line) {
  uint8_t sum = 0;
  for (size_t k = 0; k < line.size(); ++k) sum ^= static_cast<uint8_t>(line[k]);
  return sum;
}

void sendUnoStatus(uint32_t nowMs) {
  const std::string line = g_classroom.statusLine(sntch_fw::uptimeMs());
  char frame[96];
  std::snprintf(frame, sizeof(frame), "%s*%02X\n", line.c_str(),
                static_cast<unsigned>(statusChecksum(line)));
  Serial2.print(frame);
  g_lastUnoStatusMs = nowMs;
}
#endif

void logReady(bool recovered) {
  const String name = deviceName();
  Serial.printf("[sntch] ready; network \"%s\" at http://%s/\n", SNTCH_AP_SSID,
                g_ap.ip().c_str());
  Serial.printf("[sntch] device name %s\n", name.c_str());
  Serial.println("[sntch] teacher sign-in uses the PIN configured at build time");
  if (recovered) Serial.println("[sntch] Wi-Fi and HTTP API service recovered");
  g_hasReportedReady = true;
}

void attemptRecovery(uint32_t nowMs) {
  const bool wasReady = g_network.ready();
  if (g_network.accessPointReady() && !g_ap.isRunning()) {
    g_network.setAccessPointReady(false);
    Serial.println("[sntch] Wi-Fi access point lost; service unavailable until Wi-Fi recovers");
  }

  const sntch_fw::NetworkRecovery::Action action =
      g_network.nextAction(nowMs, kApRetryIntervalMs);
  if (action == sntch_fw::NetworkRecovery::Action::kNone) return;

  g_network.noteAttempt(nowMs);
  if (action == sntch_fw::NetworkRecovery::Action::kStartAccessPoint) {
    if (!g_ap.begin()) {
      g_network.setAccessPointReady(false);
      Serial.println("[sntch] ERROR: Wi-Fi access point startup failed; retrying in 5 seconds");
      return;
    }
    g_network.setAccessPointReady(true);
    refreshDeviceInfo();
    Serial.println("[sntch] Wi-Fi access point is up");
  }

  // Keep an already-started HTTP listener across AP recovery. Retry only the
  // component whose startup failed.
  if (g_network.accessPointReady() && !g_network.apiReady()) {
    refreshDeviceInfo();
    if (!g_server.begin()) {
      g_network.setApiReady(false);
      Serial.println("[sntch] ERROR: HTTP API startup failed; Wi-Fi remains up; retrying API in 5 seconds");
      return;
    }
    g_network.setApiReady(true);
  }

  if (!wasReady && g_network.ready()) logReady(g_hasReportedReady);
}

}  // namespace

void setup() {
  Serial.begin(SNTCH_SERIAL_BAUD);
  delay(150);
  Serial.println();
  Serial.printf("[sntch] SNTCH controller %s booting\n", SNTCH_FIRMWARE_VERSION);

  if (!g_storage.begin()) {
    Serial.println("[sntch] FATAL: storage unavailable; classroom service will not start");
    Serial.println("[sntch] initialize a blank filesystem explicitly with the matching PlatformIO uploadfs target");
    return;
  }

  g_classroom.setSeed(esp_random());
  bool loaded = g_classroom.load();
  Serial.printf("[sntch] classroom state %s (%u students, %u submissions)\n",
                loaded ? "loaded/initialized" : g_classroom.lastError().c_str(),
                static_cast<unsigned>(g_classroom.studentCount()),
                static_cast<unsigned>(g_classroom.submissionCount()));
  if (!loaded) {
    Serial.printf("[sntch] FATAL: classroom state could not be loaded (%s); service will not start\n",
                  g_classroom.lastError().c_str());
    Serial.println("[sntch] stored files were left in place; inspect or back up LittleFS before recovery");
    return;
  }

  g_classroomReady = true;
  g_lastSaveMs = millis();
  g_lastDiagMs = millis();
#if SNTCH_UNO_STATUS
  Serial2.begin(SNTCH_UNO_BAUD, SERIAL_8N1, SNTCH_UNO_RX_PIN, SNTCH_UNO_TX_PIN);
  sendUnoStatus(millis());
#endif
  attemptRecovery(millis());
}

void loop() {
  if (!g_classroomReady) {
    delay(1000);
    return;
  }

  attemptRecovery(millis());

  if (g_network.apiReady()) g_server.handle();
  g_classroom.tick(sntch_fw::uptimeMs());

  // Checkpoint in-progress exam clocks; mutations already persist themselves.
  if (g_classroom.attemptCount() > 0 &&
      static_cast<uint32_t>(millis() - g_lastSaveMs) >= SNTCH_SAVE_INTERVAL_MS) {
    g_classroom.save();
    g_lastSaveMs = millis();
  }

#if SNTCH_UNO_STATUS
  if (static_cast<uint32_t>(millis() - g_lastUnoStatusMs) >= SNTCH_UNO_STATUS_INTERVAL_MS) {
    sendUnoStatus(millis());
  }
#endif

  if (static_cast<uint32_t>(millis() - g_lastDiagMs) >= SNTCH_DIAG_INTERVAL_MS) {
    refreshDeviceInfo();
    logDiagnostics(g_device.name.c_str());
    g_lastDiagMs = millis();
  }

  delay(1);  // yield to the Wi-Fi stack
}
