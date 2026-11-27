// Generated from hardware/config.json by tools/generate_panel.mjs.
#pragma once
#include <stdint.h>
namespace panel {
constexpr uint32_t Baud=9600, DebounceMs=25, LongMs=700, RestartMs=2500, PowerMs=3000;
constexpr uint8_t UnoUp=2;
constexpr uint8_t UnoDown=3;
constexpr uint8_t UnoMenu=4;
constexpr uint8_t UnoSelect=5;
constexpr uint8_t UnoBacklight=6;
constexpr uint8_t UnoTftReset=8;
constexpr uint8_t UnoTftDc=9;
constexpr uint8_t UnoTftCs=10;
constexpr uint8_t UnoTftMosi=11;
constexpr uint8_t UnoTftSck=13;
constexpr uint8_t UnoUartRx=14;
constexpr uint8_t UnoUartTx=15;
constexpr uint8_t EspUartRx=16;
constexpr uint8_t EspUartTx=17;
constexpr uint8_t SoloSck=18;
constexpr uint8_t SoloMosi=23;
constexpr uint8_t SoloTftCs=22;
constexpr uint8_t SoloTftDc=21;
constexpr uint8_t SoloTftReset=17;
constexpr uint8_t SoloUp=25;
constexpr uint8_t SoloDown=26;
constexpr uint8_t SoloMenu=33;
constexpr uint8_t SoloSelect=32;
constexpr uint8_t SoloSdMiso=19;
constexpr uint8_t SoloSdCs=27;
constexpr uint8_t Width=160, Height=128, Rotation=1;
}
#define SNTCH_PANEL_TFT_INIT INITR_BLACKTAB
#ifndef SNTCH_PANEL_BACKLIGHT
#define SNTCH_PANEL_BACKLIGHT 0
#endif
