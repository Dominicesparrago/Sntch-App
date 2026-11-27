#pragma once
#include <Arduino.h>
#include <PanelPins.h>
struct SPIClass {
  bool began=false;
  bool begin(int8_t clk,int8_t miso,int8_t mosi,int8_t cs) {
    assert(clk==panel::SoloSck && mosi==panel::SoloMosi && cs==panel::SoloTftCs);
#if SNTCH_PHONE_ADMIN
    assert(miso==panel::SoloSdMiso && digitalRead(panel::SoloSdCs)==HIGH);
#else
    assert(miso==-1);
#endif
    assert(digitalRead(panel::SoloTftCs)==HIGH);
    began=true;
    return true;
  }
};
extern SPIClass SPI;
