#pragma once
#include "PanelPins.h"

namespace panel {
// UP, DOWN, MENU, SELECT occupy bits 0..3. Individual events fire on release,
// so a partner can join a hold without leaking a press or a long-press action.
class Buttons {
  uint8_t raw = 0, stable = 0, suppress = 0, combo = 0;
  bool latched = false;
  uint32_t bounce[4] = {}, down[4] = {}, began = 0;
 public:
  enum Event : uint8_t { None = 0, Up = 1, Down, Menu, Select, LongUp, LongDown, LongMenu, LongSelect, RestartHold, PowerHold };
  uint8_t holding() const { return combo == 3 ? PowerHold : combo == 12 ? RestartHold : None; }
  uint16_t progress(uint32_t now) const {
    if (!combo) return 0;
    const uint32_t ms = now-began, limit = combo == 3 ? PowerMs : RestartMs;
    return ms >= limit ? 1000 : ms*1000/limit;
  }
  template<class Emit> void scan(uint8_t mask, uint32_t now, Emit emit) {
    uint8_t released = 0;
    for (uint8_t i = 0; i < 4; ++i) {
      uint8_t bit = 1 << i;
      if ((mask ^ raw) & bit) bounce[i] = now;
      if (!((mask ^ stable) & bit) || now-bounce[i] < DebounceMs) continue;
      stable ^= bit;
      if (stable & bit) down[i] = now; else released |= bit;
    }
    raw = mask;
    if (stable && (stable & (stable-1))) {
      suppress |= stable;
      // Exact pairs only. A third key cancels the gesture until all are up.
      if ((stable != 3 && stable != 12) || (combo && combo != stable)) latched = true;
      if (!latched && !combo) { combo = stable; began = now; }
    }
    if (combo && stable != combo) { combo = 0; latched = true; }
    if (combo && !latched && raw == combo && progress(now) == 1000) {
      latched = true;
      emit(Event(holding()));
    }
    for (uint8_t i = 0; i < 4; ++i) {
      uint8_t bit = 1 << i;
      if ((released & bit) && !(suppress & bit) && !latched)
        emit(Event((now-down[i] >= LongMs ? LongUp : Up)+i));
    }
    if (!stable) { latched = false; combo = 0; suppress = 0; }
  }
};
}
