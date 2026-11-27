#pragma once
#include <PanelPins.h>
constexpr uint8_t ST77XX_DISPOFF=0x28, ST77XX_DISPON=0x29, ST77XX_SLPIN=0x10, ST77XX_SLPOUT=0x11;
constexpr uint8_t INITR_BLACKTAB=2;
struct SPIClass;
extern bool fakeTftInit;
struct Adafruit_ST7735 {
  uint16_t pixels[panel::Height][panel::Width] = {};
  unsigned y=0, rows=0;
  uint8_t lastCommand=0;
  Adafruit_ST7735()=default;
  Adafruit_ST7735(SPIClass*,uint8_t cs,uint8_t dc,uint8_t rst) {
    assert(cs==panel::SoloTftCs && dc==panel::SoloTftDc && rst==panel::SoloTftReset);
  }
  void initR(uint8_t) {fakeTftInit=true;}
  void setRotation(uint8_t r) {assert(r==panel::Rotation);}
  void setSPISpeed(uint32_t hz) {assert(hz==8000000);}
  void sendCommand(uint8_t cmd) { lastCommand=cmd; }
  void startWrite() {}
  void endWrite() {}
  void setAddrWindow(unsigned x,unsigned row,unsigned width,unsigned height) {
    assert(x==0 && row<panel::Height && width==panel::Width && height==1); y=row;
  }
  void writePixels(uint16_t* line,unsigned n) {
    assert(n==panel::Width);
    std::copy(line,line+n,pixels[y]); ++rows;
  }
};
