#pragma once

#include <Arduino.h>

namespace sntch_fw {

// millis() wraps every ~49 days. The classroom clock is monotonic 64-bit so a
// session or examination can never jump backwards because of the wrap.
inline uint64_t uptimeMs() {
  static uint32_t last = 0;
  static uint64_t high = 0;
  uint32_t now = millis();
  if (now < last) high += 0x100000000ULL;
  last = now;
  return high + now;
}

}  // namespace sntch_fw
