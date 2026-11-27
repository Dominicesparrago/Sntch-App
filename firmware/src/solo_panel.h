#pragma once
#if SNTCH_SOLO_PANEL
#include <PanelButtons.h>
#include <PanelUi.h>
#include "panel_offers.h"

namespace sntch_fw {
class SoloPanel {
 public:
  SoloPanel();
  void begin();
  void poll(sntch::Classroom& room,bool wifi,bool service,bool fs,
            uint8_t clients,uint8_t used,const char* ip,const char* ssid);
  panel_ui::Scene scene() const { return ui.state(); }
  const panel_ui::Data& displayState() const { return data; }

 private:
  Adafruit_ST7735 tft;
  panel_ui::Data data;
  panel_ui::Ui ui;
  panel::Buttons buttons;
  PanelOffers offers;
  uint32_t started=0, lastStatus=0, lastQr=0, shownRevision=0, recoveredAt=0,
           eventUntil=0, offAt=0, rebootAt=0;
  bool sleeping=false, tftAsleep=false, manualFace=false, seenClient=false;
  bool haveStatus=false, submitPulse=false, bootedService=false;

  void navigate(uint8_t view,uint8_t index,uint32_t now,bool entering=true);
  void face(uint8_t value,uint32_t now);
  void syncStatus(const sntch::Classroom&,sntch::Ms,bool,bool,bool,uint8_t,uint8_t,const char*,uint32_t);
  void showQr(sntch::Classroom&,sntch::Ms,bool,const char*,uint8_t,bool,uint32_t);
  void gesture(panel::Buttons::Event,sntch::Classroom&,sntch::Ms,bool,bool,const char*,uint32_t);
  void power(sntch::Classroom&,bool,uint32_t);
};
}
#endif
