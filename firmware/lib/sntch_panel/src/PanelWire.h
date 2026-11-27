#pragma once
#include <stdint.h>
#include <string.h>

namespace panel {
constexpr uint8_t Version = 1, Max = 32, Chunk = Max - 10, MaxSide = 37;
constexpr uint16_t MatrixMax = (MaxSide * MaxSide + 7) / 8;
enum Type : uint8_t {
  Hello = 1, Status, Button, GetQr, RestartQr, ReadQr, Standby, Wake, Reboot,
  HelloAck = 0x81, Snapshot, Ack, QrInfo, QrBits, Error
};
enum Fault : uint8_t { Reset = 1, Invalid, Offline, NoSession, Ambiguous, TooLarge, Store, Stale, Timeout };
enum Flag : uint8_t { Wifi = 1, Service = 2, Storage = 4, Paused = 8, Pairing = 16, Done = 32, Live = 64, TeacherLogin = 128 };
enum Face : uint8_t { Boot, Idle, Searching, Ready, Active, Working, Attention, Teacher, Saved, Complete, Lost, Recovery, Faces };
inline uint16_t get16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t get32(const uint8_t* p) { return get16(p) | uint32_t(get16(p + 2)) << 16; }
inline void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
inline uint16_t crcByte(uint16_t crc, uint8_t b) {
  crc ^= uint16_t(b) << 8;
  for (uint8_t i = 0; i < 8; ++i) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  return crc;
}
inline uint16_t crc(const uint8_t* p, uint16_t n) {
  uint16_t c = 0xffff;
  while (n--) c = crcByte(c, *p++);
  return c;
}
struct Frame {
  uint8_t type = 0, size = 0;
  uint16_t seq = 0;
  uint8_t data[Max];
};
class Reader {
  uint8_t b[Max + 7], n = 0;
  bool started = false, escape = false;
  uint32_t last = 0;
 public:
  bool feed(uint8_t c, uint32_t now, Frame& f) {
    if (uint32_t(now - last) > 80) started = false;
    last = now;
    if (c == 0x7e) {
      bool good = started && !escape && n >= 7 && b[0] == Version &&
        b[4] <= Max && n == b[4] + 7 && get16(b+n-2) == crc(b,n-2);
      if (good) {
        f.type = b[1]; f.seq = get16(b+2); f.size = b[4];
        memcpy(f.data,b+5,f.size);
      }
      started = true; escape = false; n = 0;
      return good;
    }
    if (!started) return false;
    if (!escape && c == 0x7d) { escape = true; return false; }
    if (n == sizeof b || (escape && c != 0x5e && c != 0x5d)) { started = false; return false; }
    b[n++] = escape ? c ^ 0x20 : c;
    escape = false;
    return false;
  }
};
template<class Port> void escaped(Port& port, uint8_t c) {
  if (c == 0x7e || c == 0x7d) { port.write(uint8_t(0x7d)); c ^= 0x20; }
  port.write(c);
}
template<class Port> void send(Port& port, const Frame& f) {
  const uint8_t h[] = {Version, f.type, uint8_t(f.seq), uint8_t(f.seq >> 8), f.size};
  uint16_t c = crc(h, sizeof h);
  port.write(uint8_t(0x7e));
  for (uint8_t b : h) escaped(port, b);
  for (uint8_t i = 0; i < f.size; ++i) { c = crcByte(c,f.data[i]); escaped(port,f.data[i]); }
  escaped(port,c); escaped(port,c >> 8);
  port.write(uint8_t(0x7e));
}
inline bool qrSide(uint8_t n) { return n >= 21 && n <= MaxSide && (n-21)%4 == 0; }
inline uint16_t qrBytes(uint8_t n) { return (uint16_t(n)*n+7)/8; }
inline uint8_t qrScale(uint8_t n) { return 90/(n+8); }
inline bool newer(uint16_t incoming,uint16_t previous) {
  const uint16_t diff = incoming - previous;
  return diff && diff < 0x8000;
}
}
