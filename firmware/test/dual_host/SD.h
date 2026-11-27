#pragma once

#include <SPI.h>

struct FakeSd {
  bool present = true;
  bool begin(uint8_t cs, SPIClass& spi, uint32_t, const char*, uint8_t, bool format) {
    assert(spi.began && cs == panel::SoloSdCs && !format);
    return present;
  }
  void end() {}
  uint64_t usedBytes() { return 3ULL << 30; }
  uint64_t totalBytes() { return 32ULL << 30; }
};
extern FakeSd SD;
