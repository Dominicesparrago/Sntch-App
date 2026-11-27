#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>
using std::min;
using std::max;
inline void utoa(unsigned v,char* out,int base) { assert(base==10); std::sprintf(out,"%u",v); }
constexpr int SERIAL_8N1=0;
extern uint32_t fakeNow;
inline uint32_t millis() { return fakeNow; }
inline uint32_t micros() { return fakeNow*1000U; }
constexpr uint8_t LOW=0,HIGH=1,INPUT_PULLUP=2,OUTPUT=3;
extern std::array<uint8_t,40> pinLevels,pinModes;
inline int digitalRead(uint8_t pin) { return pinLevels.at(pin); }
inline void digitalWrite(uint8_t pin,uint8_t value) { pinLevels.at(pin)=value; }
inline void pinMode(uint8_t pin,uint8_t mode) { pinModes.at(pin)=mode; }
struct FakeUart {
  std::deque<uint8_t> input;
  std::vector<uint8_t> output;
  void begin(unsigned baud,int,int rx,int tx) { assert(baud==9600 && rx==16 && tx==17); }
  int available() { return input.size(); }
  int read() { const auto b=input.front(); input.pop_front(); return b; }
  void write(uint8_t b) { output.push_back(b); }
};
extern FakeUart Serial2;
struct FakeEsp { unsigned resets=0; void restart(){ ++resets; } };
extern FakeEsp ESP;

struct FakeSerial {
  void println(const char* message) { std::puts(message); }
  template <class... Args>
  void printf(const char* format, Args... args) { std::printf(format, args...); }
};
extern FakeSerial Serial;
