// Standalone classic ESP32-WROOM-32D + ST7735 wiring check.
//
// This is a separate sketch from ../src/main.ino and arduino-controls.ino. It
// exercises the TFT and four buttons directly on the
// classic ESP32. It does not
// implement SNTCH networking or communicate with an Arduino Uno.
//
// TFT SPI: SCK=GPIO18, MOSI/SDA=GPIO23, CS=GPIO22, DC/A0=GPIO21, RST=GPIO17.
// Buttons: Select=GPIO32, Back=GPIO33, Left=GPIO25, Right=GPIO26; other leg to GND.
// Buttons use INPUT_PULLUP, so pressed reads LOW. No MISO is needed for display-only SPI.
// All GPIO signals are 3.3 V. Check the exact TFT breakout's VCC/backlight requirements.

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

void setup();
void loop();

namespace {

constexpr uint8_t kTftSck = 18;
constexpr uint8_t kTftMosi = 23;
constexpr uint8_t kTftCs = 22;
constexpr uint8_t kTftDc = 21;
constexpr uint8_t kTftRst = 17;

constexpr uint8_t kSelectPin = 32;
constexpr uint8_t kBackPin = 33;
constexpr uint8_t kLeftPin = 25;
constexpr uint8_t kRightPin = 26;
constexpr uint32_t kDebounceMs = 30;

Adafruit_ST7735 g_tft(kTftCs, kTftDc, kTftRst);

struct Button {
  uint8_t pin;
  bool raw;
  bool stable;
  uint32_t changedAt;
};

Button g_buttons[] = {
    {kSelectPin, HIGH, HIGH, 0},
    {kBackPin, HIGH, HIGH, 0},
    {kLeftPin, HIGH, HIGH, 0},
    {kRightPin, HIGH, HIGH, 0},
};

const char* const kMenu[] = {"Activities", "Exams", "Settings"};
constexpr uint8_t kMenuCount = sizeof(kMenu) / sizeof(kMenu[0]);
uint8_t g_selected = 0;
bool g_showingSelection = false;

bool pressed(Button& button, uint32_t now) {
  const bool reading = digitalRead(button.pin);
  if (reading != button.raw) {
    button.raw = reading;
    button.changedAt = now;
  }
  if (static_cast<uint32_t>(now - button.changedAt) < kDebounceMs ||
      reading == button.stable) {
    return false;
  }
  button.stable = reading;
  return reading == LOW;
}

void drawMenu() {
  g_showingSelection = false;
  g_tft.fillScreen(ST77XX_BLACK);
  g_tft.setTextWrap(false);
  g_tft.setTextSize(1);
  g_tft.setTextColor(ST77XX_CYAN);
  g_tft.setCursor(8, 8);
  g_tft.print("SNTCH WROOM TFT");
  g_tft.drawFastHLine(6, 24, 116, 0x4208);

  for (uint8_t i = 0; i < kMenuCount; ++i) {
    const int16_t y = 38 + i * 25;
    const uint16_t color = (i == g_selected) ? ST77XX_BLUE : ST77XX_BLACK;
    g_tft.fillRoundRect(8, y - 3, 112, 20, 3, color);
    g_tft.setTextColor(ST77XX_WHITE);
    g_tft.setCursor(16, y + 3);
    g_tft.print(i == g_selected ? "> " : "  ");
    g_tft.print(kMenu[i]);
  }

  g_tft.setTextColor(ST77XX_YELLOW);
  g_tft.setCursor(5, 125);
  g_tft.print("L/R move  Select OK");
  g_tft.setCursor(5, 140);
  g_tft.print("Back returns here");
}

void showSelection() {
  g_showingSelection = true;
  g_tft.fillScreen(ST77XX_BLACK);
  g_tft.setTextColor(ST77XX_GREEN);
  g_tft.setTextSize(1);
  g_tft.setCursor(8, 12);
  g_tft.print("Button test OK");
  g_tft.setTextColor(ST77XX_WHITE);
  g_tft.setCursor(8, 42);
  g_tft.print("Selected:");
  g_tft.setTextColor(ST77XX_CYAN);
  g_tft.setTextSize(2);
  g_tft.setCursor(8, 60);
  g_tft.print(kMenu[g_selected]);
  g_tft.setTextSize(1);
  g_tft.setTextColor(ST77XX_YELLOW);
  g_tft.setCursor(8, 132);
  g_tft.print("Press Back for menu");
  Serial.printf("[tft-test] selected %s\n", kMenu[g_selected]);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);

  for (Button& button : g_buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.raw = button.stable = digitalRead(button.pin);
    button.changedAt = millis();
  }

  SPI.begin(kTftSck, -1, kTftMosi, kTftCs);
  g_tft.initR(INITR_BLACKTAB);
  g_tft.setRotation(0);
  drawMenu();

  Serial.println("[tft-test] ready; ESP32-WROOM-32D + ST7735");
}

void loop() {
  const uint32_t now = millis();
  if (pressed(g_buttons[0], now)) showSelection();
  if (pressed(g_buttons[1], now)) drawMenu();

  if (pressed(g_buttons[2], now)) {
    g_selected = (g_selected + kMenuCount - 1) % kMenuCount;
    drawMenu();
  }
  if (pressed(g_buttons[3], now)) {
    g_selected = (g_selected + 1) % kMenuCount;
    drawMenu();
  }

  delay(2);
}
