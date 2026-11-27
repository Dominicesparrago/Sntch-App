// SNTCH controller firmware for classic ESP32. Option C uses TFT and SPI microSD.
//
// Boot: mount classroom storage, load the snapshot, start the SNTCH Wi-Fi
// access point, serve the local HTTP/JSON API. All classroom logic lives in
// lib/sntch_core, which is also compiled and tested on the host.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "access_point.h"
#include "api_server.h"
#include "network_recovery.h"
#include "sntch/api.hpp"
#include "sntch/classroom.hpp"
#include "sntch_config.h"
#include "uptime.h"
#include "dual_panel.h"
#include "solo_panel.h"
#if SNTCH_PHONE_ADMIN
#include "sd_storage.h"
#include "phone_admin_panel.h"
#else
#include "littlefs_storage.h"
#endif

// Keep Arduino's sketch preprocessor from generating setup()/loop() prototypes
// inside the private namespace below.
void setup();
void loop();

using sntch::Classroom;
using sntch::DeviceInfo;

namespace {

#if SNTCH_PHONE_ADMIN
sntch_fw::SdStorage g_storage;
#else
sntch_fw::LittleFsStorage g_storage;
#endif
Classroom g_classroom(&g_storage, SNTCH_TEACHER_PIN);
sntch_fw::AccessPoint g_ap;
DeviceInfo g_device;
sntch::Api g_api(&g_classroom, g_device);
sntch_fw::ApiServer g_server(&g_api, &g_classroom);
#if SNTCH_PHONE_ADMIN
sntch_fw::PhoneAdminPanel g_panel;
#elif SNTCH_SOLO_PANEL
sntch_fw::SoloPanel g_panel;
#elif SNTCH_DUAL_PANEL
sntch_fw::DualPanel g_panel;
#endif
#if SNTCH_DUAL_PANEL || SNTCH_SOLO_PANEL || SNTCH_PHONE_ADMIN
bool g_fsMounted = false;
#endif
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

#if SNTCH_DUAL_PANEL || SNTCH_SOLO_PANEL || SNTCH_PHONE_ADMIN
void servicePanel() {
  static uint32_t last = 0;
  static bool lastAp = false;
  static String ip;
  static uint8_t clients = 0, used = 0;
  const bool ap=g_network.accessPointReady();
  if (ap!=lastAp || millis()-last>=1000) {
    last=millis(); lastAp=ap;
    ip=ap?g_ap.ip():String();
    clients=ap?g_ap.clientCount():0;
    const uint64_t total=g_fsMounted?g_storage.totalBytes():0;
    used=total?g_storage.usedBytes()*100/total:0;
  }
  g_panel.poll(g_classroom,ap,g_classroomReady&&g_network.ready(),g_fsMounted&&g_classroomReady,
               clients,used,ip.c_str(),SNTCH_AP_SSID);
}
#endif

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
  Serial.printf("[sntch] up=%lus heap=%uB clients=%d students=%u sessions=%u subs=%u storage=%llu/%llu%s\n",
                static_cast<unsigned long>(sntch_fw::uptimeMs() / 1000),
                static_cast<unsigned>(ESP.getFreeHeap()), clients,
                static_cast<unsigned>(g_classroom.studentCount()),
                static_cast<unsigned>(g_classroom.openSessionCount()),
                static_cast<unsigned>(g_classroom.submissionCount()),
                static_cast<unsigned long long>(g_storage.usedBytes()),
                static_cast<unsigned long long>(g_storage.totalBytes()),
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
// Lambda avoids the .ino preprocessor emitting unconditional prototypes for
// a function that's excluded from the dual target.
const auto sendUnoStatus = [](uint32_t nowMs) {
  const std::string line = g_classroom.statusLine(sntch_fw::uptimeMs());
  uint8_t sum = 0;
  for (const unsigned char b : line) sum ^= b;
  char frame[96];
  std::snprintf(frame, sizeof(frame), "%s*%02X\n", line.c_str(),
                static_cast<unsigned>(sum));
  Serial2.print(frame);
  g_lastUnoStatusMs = nowMs;
};
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
#if SNTCH_DUAL_PANEL || SNTCH_SOLO_PANEL || SNTCH_PHONE_ADMIN
  g_panel.begin();
#endif

  if (!g_storage.begin()) {
    Serial.println("[sntch] FATAL: storage unavailable; classroom service will not start");
#if SNTCH_PHONE_ADMIN
    Serial.println("[sntch] check the microSD card, FAT32 filesystem and /sntch directory");
#else
    Serial.println("[sntch] initialize a blank filesystem explicitly with the matching PlatformIO uploadfs target");
#endif
    return;
  }
#if SNTCH_DUAL_PANEL || SNTCH_SOLO_PANEL || SNTCH_PHONE_ADMIN
  g_fsMounted=true;
#endif

  g_classroom.setSeed(esp_random());
  bool loaded = g_classroom.load();
#if SNTCH_PHONE_ADMIN
  if (!g_storage.healthy()) {
    g_fsMounted = false;
    Serial.println("[sntch] FATAL: microSD I/O error while loading classroom state");
    return;
  }
#endif
  Serial.printf("[sntch] classroom state %s (%u students, %u submissions)\n",
                loaded ? "loaded/initialized" : g_classroom.lastError().c_str(),
                static_cast<unsigned>(g_classroom.studentCount()),
                static_cast<unsigned>(g_classroom.submissionCount()));
  if (!loaded) {
    Serial.printf("[sntch] FATAL: classroom state could not be loaded (%s); service will not start\n",
                  g_classroom.lastError().c_str());
#if SNTCH_PHONE_ADMIN
    Serial.println("[sntch] stored files were left in place; inspect or back up the SD card before recovery");
#else
    Serial.println("[sntch] stored files were left in place; inspect or back up LittleFS before recovery");
#endif
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
#if SNTCH_PHONE_ADMIN
  if (g_classroomReady && !g_storage.healthy()) {
    g_classroomReady = false;
    g_fsMounted = false;
    Serial.println("[sntch] classroom service stopped after a microSD error; check the card and restart");
  }
#endif
  if (!g_classroomReady) {
#if SNTCH_DUAL_PANEL || SNTCH_SOLO_PANEL || SNTCH_PHONE_ADMIN
    servicePanel();
    delay(2);
#else
    delay(1000);
#endif
    return;
  }

  attemptRecovery(millis());

  if (g_network.apiReady()) g_server.handle();
  g_classroom.tick(sntch_fw::uptimeMs());
#if SNTCH_DUAL_PANEL || SNTCH_SOLO_PANEL || SNTCH_PHONE_ADMIN
  servicePanel();
#endif

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
