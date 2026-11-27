#pragma once
#if SNTCH_DUAL_PANEL
#include <Arduino.h>
#include <PanelWire.h>
#include "sntch/classroom.hpp"
#include "panel_offers.h"

namespace sntch_fw {
class DualPanel {
 public:
  void begin();
  void poll(sntch::Classroom& classroom, bool wifi, bool service, bool fs,
            uint8_t clients, uint8_t usedPct, const char* ip, const char* ssid);
 private:
  panel::Reader reader;
  panel::Frame in, previous, reply, cached;
  uint32_t boot = 0, peer = 0, rebootAt = 0;
  PanelOffers offers;
  bool havePeer = false, havePrevious = false, standby = false;
  bool seenClient = false;
  void error(panel::Fault);
  void response(panel::Type, uint8_t len);
};
}
#endif
