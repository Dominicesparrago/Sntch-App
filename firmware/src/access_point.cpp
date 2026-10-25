#include "access_point.h"

#include <esp_wifi.h>

#include "sntch_config.h"

namespace sntch_fw {

bool AccessPoint::begin() {
  ssid_ = SNTCH_AP_SSID;

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);  // keep response latency low for polling clients
  WiFi.setHostname(SNTCH_AP_HOSTNAME);

  bool ok = WiFi.softAP(ssid_.c_str(), SNTCH_AP_PASSWORD, SNTCH_AP_CHANNEL, /*hidden=*/false,
                        SNTCH_AP_MAX_CLIENTS);
  if (!ok) {
    Serial.println("[sntch] softAP() failed");
    return false;
  }

  // The ESP32-S3 access point tops out at 10 associated stations; that ceiling,
  // not the data model, is the real classroom size for one controller.
  wifi_config_t config;
  if (esp_wifi_get_config(WIFI_IF_AP, &config) == ESP_OK) {
    config.ap.max_connection = SNTCH_AP_MAX_CLIENTS;
    esp_wifi_set_config(WIFI_IF_AP, &config);
  }

  Serial.printf("[sntch] access point \"%s\" up at %s (max %d clients)\n", ssid_.c_str(),
                WiFi.softAPIP().toString().c_str(), SNTCH_AP_MAX_CLIENTS);
  return true;
}

int AccessPoint::clientCount() const { return WiFi.softAPgetStationNum(); }

String AccessPoint::ip() const { return WiFi.softAPIP().toString(); }

}  // namespace sntch_fw
