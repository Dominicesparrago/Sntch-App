#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace sntch_fw {

// Owns the SNTCH Wi-Fi network. The controller is the only access point in V1;
// no router and no internet connection are required.
class AccessPoint {
 public:
  bool begin();
  int clientCount() const;
  String ssid() const { return ssid_; }
  String ip() const;

 private:
  String ssid_;
};

}  // namespace sntch_fw
