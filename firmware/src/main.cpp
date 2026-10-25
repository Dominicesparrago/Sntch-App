// SNTCH controller firmware (ESP32-S3).
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
#include "sntch/api.hpp"
#include "sntch/classroom.hpp"
#include "sntch_config.h"
#include "uptime.h"

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
int g_lastClients = -1;

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

}  // namespace

void setup() {
  Serial.begin(SNTCH_SERIAL_BAUD);
  delay(150);
  Serial.println();
  Serial.printf("[sntch] SNTCH controller %s booting\n", SNTCH_FIRMWARE_VERSION);

  if (!g_storage.begin()) {
    Serial.println("[sntch] running without persistent storage (RAM only)");
  }

  g_classroom.setSeed(esp_random());
  bool loaded = g_classroom.load();
  Serial.printf("[sntch] classroom state %s (%u students, %u submissions)\n",
                loaded ? "loaded" : "empty/corrupt",
                static_cast<unsigned>(g_classroom.studentCount()),
                static_cast<unsigned>(g_classroom.submissionCount()));

  if (!g_ap.begin()) Serial.println("[sntch] WARNING: access point failed to start");

  const String name = deviceName();
  g_device = collectDeviceInfo(name);
  g_api.setDeviceInfo(g_device);

  g_server.begin();
  g_lastSaveMs = millis();
  g_lastDiagMs = millis();

  Serial.println("[sntch] ready");
  Serial.printf("[sntch] network \"%s\" (WPA2) - open the SNTCH app and connect\n", SNTCH_AP_SSID);
  Serial.printf("[sntch] controller at http://%s/\n", g_ap.ip().c_str());
  Serial.printf("[sntch] device name %s\n", name.c_str());
  Serial.println("[sntch] teacher sign-in uses the PIN configured at build time");
}

void loop() {
  g_server.handle();
  g_classroom.tick(sntch_fw::uptimeMs());

  // Checkpoint in-progress exam clocks; mutations already persist themselves.
  if (g_classroom.attemptCount() > 0 &&
      static_cast<uint32_t>(millis() - g_lastSaveMs) >= SNTCH_SAVE_INTERVAL_MS) {
    g_classroom.save();
    g_lastSaveMs = millis();
  }

  if (static_cast<uint32_t>(millis() - g_lastDiagMs) >= SNTCH_DIAG_INTERVAL_MS) {
    g_device = collectDeviceInfo(deviceName());
    g_api.setDeviceInfo(g_device);
    logDiagnostics(g_device.name.c_str());
    g_lastDiagMs = millis();
  }

  delay(1);  // yield to the Wi-Fi stack
}
