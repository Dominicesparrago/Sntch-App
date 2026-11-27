#pragma once

#include <PanelUi.h>
#include "panel_offers.h"

namespace sntch_fw {

class PhoneAdminPanel {
 public:
  PhoneAdminPanel();
  void begin();
  void poll(sntch::Classroom& room, bool wifi, bool service, bool storage,
            uint8_t clients, uint8_t used, const char* ip, const char* ssid);
  const panel_ui::Data& displayState() const { return data; }

 private:
  Adafruit_ST7735 tft;
  panel_ui::Data data;
  panel_ui::Ui ui;
  PanelOffers offers;
  uint32_t started = 0;
  uint32_t lastStatus = 0;
  uint32_t shownRevision = 0;

  void update(sntch::Classroom& room, bool wifi, bool service, bool storage,
              const char* ip, const char* ssid, uint32_t now);
};

}  // namespace sntch_fw
