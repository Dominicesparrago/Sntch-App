// =====================================================================
//  SNTCH  -  Arduino Uno R3 + 1.8" ST7735 TFT
//  Boot intro -> face -> menu -> idle sleep, with hold-Select power on/off
//
//  TFT   SCK->D13  SDA/MOSI->D11  CS->D10  DC/A0->D7  RST->D6
//  Buttons (other leg to GND): Select->D2  Back->D3  Left->D4  Right->D5
//  Buzzer: + -> D9, - -> GND
//  (No LEDs, no ESP32 link in this version: the Uno does everything by itself.)
//
//  Menu: Activities / Exams  -> session list (Add / open / delete)
//        inside a session    -> Join QR, Students monitor, Delete
//        QR Code             -> Wi-Fi join QR only
//  Sessions are saved in EEPROM, so they survive power-off.
//
//  Needs the "QRCode" library by Richard Moore (Library Manager), plus the
//  Adafruit GFX and ST7735 libraries you already use.
//
//  Select: short press = select / wake from idle sleep
//          hold 1.5 s  = power OFF; hold 1.5 s while off = power ON
//  Hold Select while plugging in to skip the intro.
// =====================================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <avr/sleep.h>
#include <EEPROM.h>
#include "qrcode.h"          // QRCode library by Richard Moore

#define TFT_CS   10
#define TFT_DC    7
#define TFT_RST   6

// Display variant: INITR_BLACKTAB, INITR_GREENTAB or INITR_REDTAB
#define TFT_TAB  INITR_BLACKTAB

const uint8_t BTN_SELECT = 2;   // must stay on D2 (wakes the Uno from power-down)
const uint8_t BTN_BACK   = 3;
const uint8_t BTN_LEFT   = 4;
const uint8_t BTN_RIGHT  = 5;

const uint8_t BUZZER_PIN = 9;
#define BUZZER_PASSIVE 1   // 1 = passive piezo (tone), 0 = active buzzer

// TFT backlight: wire the display's LED/BL pin to D8 (through ~100 ohm) so SNTCH
// can switch it off. Set USE_BACKLIGHT_PIN to 0 if BL stays wired to power
// (the screen then just goes black instead of fully off).
#define USE_BACKLIGHT_PIN 1
const uint8_t BACKLIGHT_PIN = 8;

void backlight(bool on) {
#if USE_BACKLIGHT_PIN
  digitalWrite(BACKLIGHT_PIN, on ? HIGH : LOW);
#endif
}

// Serial debug messages. Costs ~1.5 KB of flash: the Uno is nearly full, so keep 0 unless debugging.
#define DEBUG_SERIAL 0
#if DEBUG_SERIAL
  #define LOG(msg) Serial.println(F(msg))
#else
  #define LOG(msg)
#endif

const unsigned long SLEEP_TIMEOUT = 10000UL;   // idle: 10 s without a press
const unsigned long HOLD_MS       = 1500UL;    // hold Select this long to toggle power
const unsigned long SESSION_TIMEOUT = 60000UL;   // session screens stay on 1 min
const unsigned long QR_TIMEOUT    = 120000UL;  // QR / student monitor stay on 2 min
const int MARGIN = 4;

// What the QR codes contain. Keep these the same as the ESP32 hotspot settings.
// (If the password has ; , : \ or " in it, put a \ in front of that character.)
#define WIFI_SSID  "SNTCH-EXAM"
#define WIFI_PASS  "sntch-1234"
#define JOIN_BASE  "http://192.168.4.1/?s="    // + 6-character session code

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

enum Screen { SCREEN_FACE, SCREEN_MENU, SCREEN_DETAIL, SCREEN_SLEEP,
              SCREEN_LIST, SCREEN_SESSION, SCREEN_QR, SCREEN_STUDENTS, SCREEN_CONFIRM };
Screen screen = SCREEN_FACE;

const uint8_t NUM_ITEMS = 5;
const char* const menuItems[NUM_ITEMS] = {"Activities", "Exams", "QR Code", "Sync", "Settings"};
const uint8_t ITEM_ACTIVITIES = 0, ITEM_EXAMS = 1, ITEM_QR = 2;   // indexes in menuItems
int8_t selected = 0;

unsigned long lastActivity = 0;

// ---------------- Text helpers ----------------
uint8_t fitSize(const char* s, uint8_t maxSize) {
  while (maxSize > 1 && (int)strlen(s) * 6 * maxSize > tft.width() - 2 * MARGIN) {
    maxSize--;
  }
  return maxSize;
}

void printCentered(const char* s, int y, uint8_t size) {
  int w = (int)strlen(s) * 6 * size;
  int x = (tft.width() - w) / 2;
  if (x < 0) x = 0;
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(s);
}

void printCenteredF(const __FlashStringHelper* s, int y, uint8_t size) {
  int w = (int)strlen_P((PGM_P)s) * 6 * size;
  int x = (tft.width() - w) / 2;
  if (x < 0) x = 0;
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(s);
}

// ---------------- Buzzer ----------------
void beep(int freq, int ms) {
#if BUZZER_PASSIVE
  tone(BUZZER_PIN, freq, ms);
#else
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms / 2);
  digitalWrite(BUZZER_PIN, LOW);
#endif
}

// ---------------- Buttons ----------------
struct Button {
  uint8_t pin;
  bool lastReading;
  bool stable;
  unsigned long changedAt;
};

Button btns[4] = {
  {BTN_SELECT, HIGH, HIGH, 0},
  {BTN_BACK,   HIGH, HIGH, 0},
  {BTN_LEFT,   HIGH, HIGH, 0},
  {BTN_RIGHT,  HIGH, HIGH, 0}
};

bool selectDown = false;        // Select currently held (debounced)
bool selectHandled = false;     // long hold already used for this press
unsigned long selectDownAt = 0;

void resetButtons() {
  for (uint8_t i = 0; i < 4; i++) {
    btns[i].lastReading = HIGH;
    btns[i].stable = HIGH;
    btns[i].changedAt = millis();
  }
  selectDown = false;
  selectHandled = false;
}

// =====================================================================
//  FACE  -  rounded-square glowing eyes (like the reference robot)
//
//  Flicker-free drawing: instead of erasing a shape and then drawing the
//  new one, every frame rewrites the whole box around the old+new eye in
//  ONE pass (new shape = EYE_COLOR, everything else = black). Each pixel is
//  written exactly once, so there is never a blank frame in between.
// =====================================================================
#define EYE_COLOR   0xFFFF      // white  (try 0x479C for soft cyan)
#define EYE_RADIUS  9           // corner roundness in pixels (bigger = rounder)
const int16_t LEFT_X  = 40;     // eye centre X
const int16_t RIGHT_X = 88;
const int16_t MOUTH_X0 = 50, MOUTH_W = 29, MOUTH_Y0 = 88, MOUTH_H = 18;
const int16_t MOUTH_MID_X = 64, MOUTH_BASE_Y = 92;

// A face is 7 plain numbers (no custom types, so the Arduino IDE's
// auto-generated prototypes always compile):
#define FACE_N  7
#define F_LW    0     // left eye  width
#define F_LH    1     //           height
#define F_LCY   2     //           centre Y
#define F_RW    3     // right eye width
#define F_RH    4     //           height
#define F_RCY   5     //           centre Y
#define F_MOUTH 6     // smile depth in pixels (0 = flat line)

//                              left eye      right eye     mouth
const float idleFace[FACE_N]    = {30, 32, 64,  30, 32, 64,    0};
const float happyFace[FACE_N]   = {32, 22, 66,  32, 22, 66,   10};
// curious: left eye shrinks a little, right eye grows big
const float curiousFace[FACE_N] = {26, 26, 66,  40, 44, 62,    0};
const float halfFace[FACE_N]    = {30, 12, 66,  30, 12, 66,    0};   // sleepy
const float closedFace[FACE_N]  = {30,  4, 66,  30,  4, 66,    0};   // eyes shut

float currentFace[FACE_N] = {30, 32, 64,  30, 32, 64, 0};
float startFace[FACE_N]   = {30, 32, 64,  30, 32, 64, 0};
float targetFace[FACE_N]  = {30, 32, 64,  30, 32, 64, 0};

void copyFace(float* dst, const float* src) {
  for (uint8_t i = 0; i < FACE_N; i++) dst[i] = src[i];
}

const unsigned long transitionDuration = 700;
unsigned long transitionStart = 0;
unsigned long nextExpressionTime = 0;
unsigned long lastFrame = 0;

// blinking
const unsigned long BLINK_MS = 170;
bool blinking = false;
unsigned long blinkStart = 0;
unsigned long nextBlink = 0;

// what is physically on the screen right now: {width, height, centreY}
int16_t drawnL[3] = {0, 0, 0};
int16_t drawnR[3] = {0, 0, 0};
int8_t drawnMouth = -1;             // -1 = unknown / needs redraw

void resetDrawn() {                 // call after anything wipes the screen
  for (uint8_t i = 0; i < 3; i++) { drawnL[i] = 0; drawnR[i] = 0; }
  drawnMouth = -1;
}

void clearScreen() {
  tft.fillScreen(ST77XX_BLACK);
  resetDrawn();
}

// Does row y cross the eye? If so, give the left/right pixel of the span.
bool eyeSpan(int16_t cx, int16_t w, int16_t h, int16_t cy, int16_t y, int16_t& L, int16_t& R) {
  if (w <= 0 || h <= 0) return false;
  int16_t top = cy - h / 2;
  int16_t t = y - top;
  if (t < 0 || t >= h) return false;

  int16_t r = EYE_RADIUS;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;

  int16_t d = t;                                  // rows from the nearest top/bottom edge
  if (h - 1 - t < d) d = h - 1 - t;

  int16_t inset = 0;
  if (d < r) {
    // inset = r - sqrt(r^2 - dy^2), dy = r - d - 0.5, done in integers (x4 scale)
    int16_t dy2 = 2 * (r - d) - 1;                       // 2*dy
    int16_t v = 4 * r * r - dy2 * dy2;                   // 4*(r^2 - dy^2)
    int16_t q = 0;
    while ((q + 1) * (q + 1) <= v) q++;                  // q = isqrt(v) = 2*sqrt(r^2 - dy^2)
    inset = r - (q + 1) / 2;
  }
  int16_t left = cx - w / 2;
  L = left + inset;
  R = left + w - 1 - inset;
  return L <= R;
}

void updateEye(int16_t cx, int16_t* old, int16_t w, int16_t h, int16_t cy) {
  if (old[0] == w && old[1] == h && old[2] == cy) return;   // nothing changed

  bool vo = (old[0] > 0 && old[1] > 0);
  bool vn = (w > 0 && h > 0);
  if (!vo && !vn) { old[0] = w; old[1] = h; old[2] = cy; return; }

  int16_t top = 32767, bot = -32768, left = 32767, right = -32768;
  if (vo) {
    int16_t t = old[2] - old[1] / 2, l = cx - old[0] / 2;
    top = t; bot = t + old[1] - 1; left = l; right = l + old[0] - 1;
  }
  if (vn) {
    int16_t t = cy - h / 2, l = cx - w / 2;
    if (t < top) top = t;
    if (t + h - 1 > bot) bot = t + h - 1;
    if (l < left) left = l;
    if (l + w - 1 > right) right = l + w - 1;
  }
  left--; right++;                                 // 1 px safety margin
  if (left < 0) left = 0;
  if (top < 0) top = 0;
  if (right >= tft.width())  right = tft.width() - 1;
  if (bot >= tft.height())   bot = tft.height() - 1;
  int16_t bw = right - left + 1;
  if (bw > 48) bw = 48;

  uint16_t buf[48];
  tft.startWrite();
  tft.setAddrWindow(left, top, bw, bot - top + 1);
  for (int16_t y = top; y <= bot; y++) {
    int16_t L = 0, R = -1;
    bool on = eyeSpan(cx, w, h, cy, y, L, R);
    for (int16_t i = 0; i < bw; i++) {
      int16_t x = left + i;
      buf[i] = (on && x >= L && x <= R) ? EYE_COLOR : ST77XX_BLACK;
    }
    tft.writePixels(buf, bw);
  }
  tft.endWrite();
  old[0] = w; old[1] = h; old[2] = cy;
}

void updateMouth(int8_t depth) {
  if (depth == drawnMouth) return;
  uint16_t buf[MOUTH_W];
  tft.startWrite();
  tft.setAddrWindow(MOUTH_X0, MOUTH_Y0, MOUTH_W, MOUTH_H);
  for (int16_t y = MOUTH_Y0; y < MOUTH_Y0 + MOUTH_H; y++) {
    for (int16_t i = 0; i < MOUTH_W; i++) {
      int16_t x = MOUTH_X0 + i;
      int16_t dx = abs(x - MOUTH_MID_X);
      bool on = false;
      if (dx <= 11) {
        int16_t yc = MOUTH_BASE_Y + ((int16_t)depth * (11 - dx) + 5) / 11;   // flat or "v"
        on = abs(y - yc) <= 1;                                               // 3 px thick
      }
      buf[i] = on ? EYE_COLOR : ST77XX_BLACK;
    }
    tft.writePixels(buf, MOUTH_W);
  }
  tft.endWrite();
  drawnMouth = depth;
}

// blink: 0 = eyes open, 1 = eyes shut
void renderFace(const float* f, float blink) {
  float k = 1.0 - 0.92 * blink;
  updateEye(LEFT_X,  drawnL, (int16_t)(f[F_LW] + 0.5), (int16_t)(f[F_LH] * k + 0.5), (int16_t)(f[F_LCY] + 0.5));
  updateEye(RIGHT_X, drawnR, (int16_t)(f[F_RW] + 0.5), (int16_t)(f[F_RH] * k + 0.5), (int16_t)(f[F_RCY] + 0.5));
  updateMouth((int8_t)(f[F_MOUTH] + 0.5));
}

float smoothStep(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }   // smootherstep
float blend(float from, float to, float amount) { return from + (to - from) * amount; }

void blendFace(const float* a, const float* b, float t) {
  for (uint8_t i = 0; i < FACE_N; i++) currentFace[i] = blend(a[i], b[i], t);
}

// Smoothly morph from the current face to another one (blocks for 'ms').
void tweenFace(const float* to, unsigned long ms) {
  float from[FACE_N];
  copyFace(from, currentFace);
  unsigned long t0 = millis();
  for (;;) {
    unsigned long e = millis() - t0;
    float p = (e >= ms) ? 1.0 : e / (float)ms;
    blendFace(from, to, smoothStep(p));
    renderFace(currentFace, 0);
    if (p >= 1.0) break;
  }
}

void pickTargetFace() {
  int choice = random(100);
  if (choice < 20)      copyFace(targetFace, happyFace);    // 20%
  else if (choice < 50) copyFace(targetFace, curiousFace);  // 30%
  else                  copyFace(targetFace, idleFace);     // 50%
}

void beginNewExpression() {
  copyFace(startFace, currentFace);
  pickTargetFace();
  transitionStart = millis();
  nextExpressionTime = transitionStart + random(1800, 3500);
}

void startFaceLoop() {
  screen = SCREEN_FACE;
  blinking = false;
  nextBlink = millis() + random(1500, 3500);
  beginNewExpression();
}

void enterFace() {
  clearScreen();
  renderFace(currentFace, 0);
  startFaceLoop();
}

void updateFace(unsigned long now) {
  if (now - lastFrame < 33) return;     // ~30 fps
  lastFrame = now;

  if (now >= nextExpressionTime) beginNewExpression();

  float progress = (now - transitionStart) / (float)transitionDuration;
  if (progress > 1.0) progress = 1.0;
  blendFace(startFace, targetFace, smoothStep(progress));

  float blink = 0;
  if (blinking) {
    unsigned long e = now - blinkStart;
    if (e >= BLINK_MS) {
      blinking = false;
      nextBlink = now + random(2200, 5000);
    } else {
      float p = e / (float)BLINK_MS;                      // 0..1
      blink = smoothStep(p < 0.5 ? p * 2.0 : (1.0 - p) * 2.0);   // close then open
    }
  } else if (now >= nextBlink) {
    blinking = true;
    blinkStart = now;
  }

  renderFace(currentFace, blink);
}

// =====================================================================
//  IDLE SLEEP (face asleep, screen on)
// =====================================================================
uint8_t zStep = 0;
unsigned long lastZStep = 0;

void drawZs(uint8_t count) {
  tft.fillRect(88, 8, 40, 56, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  if (count >= 1) { tft.setTextSize(1); tft.setCursor(94, 54); tft.print("z"); }
  if (count >= 2) { tft.setTextSize(2); tft.setCursor(100, 34); tft.print("z"); }
  if (count >= 3) { tft.setTextSize(3); tft.setCursor(104, 8);  tft.print("Z"); }
}

void enterSleep() {
  LOG("SNTCH is sleeping");

  clearScreen();
  copyFace(currentFace, idleFace);
  renderFace(currentFace, 0);
  delay(150);
  tweenFace(halfFace, 350);          // eyes droop...
  tweenFace(closedFace, 300);        // ...and close

  tft.fillRect(MOUTH_X0, MOUTH_Y0, MOUTH_W, MOUTH_H, ST77XX_BLACK);
  tft.drawCircle(64, 95, 3, EYE_COLOR);   // little sleeping "o" mouth
  drawnMouth = -1;                        // mouth area no longer matches our cache

  screen = SCREEN_SLEEP;
  zStep = 0;
  lastZStep = millis();
  drawZs(0);
}

void updateSleep(unsigned long now) {
  if (now - lastZStep < 700) return;
  lastZStep = now;
  zStep = (zStep + 1) % 4;
  drawZs(zStep);
}

void wakeUp() {
  LOG("SNTCH woke up");
  clearScreen();
  copyFace(currentFace, closedFace);
  renderFace(currentFace, 0);
  delay(150);
  beep(660, 80);
  tweenFace(halfFace, 180);
  tweenFace(idleFace, 260);
  startFaceLoop();                   // screen is already drawn, just start animating
}

// =====================================================================
//  INTRO
// =====================================================================
void playIntro() {
  clearScreen();
  delay(400);

  copyFace(currentFace, closedFace);          // thin glowing bars
  renderFace(currentFace, 0);
  delay(600);

  tweenFace(idleFace, 350);          // eyes open smoothly
  beep(660, 100);
  delay(300);

  tweenFace(closedFace, 100);        // blink
  tweenFace(idleFace, 140);
  delay(250);

  tweenFace(happyFace, 400);         // smile
  beep(880, 100);
  delay(300);

  const char title[] = "SNTCH";
  const int letterW = 18;
  int startX = (tft.width() - 5 * letterW) / 2;
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  for (uint8_t i = 0; i < 5; i++) {
    tft.setCursor(startX + i * letterW, 114);
    tft.print(title[i]);
    beep(500 + i * 120, 60);
    delay(180);
  }

  printCenteredF(F("Hello! I'm SNTCH"), 144, 1);
  beep(1047, 180);
  delay(1600);

  clearScreen();
}

// =====================================================================
//  POWER ON / OFF
// =====================================================================
void wakeISR() {}   // only needs to exist; it wakes the Uno from power-down

void waitForSelectRelease() {
  while (digitalRead(BTN_SELECT) == LOW) delay(10);
  delay(50);
}

void powerOn() {
  LOG("SNTCH powering on");
  clearScreen();
  backlight(true);
  waitForSelectRelease();
  playIntro();
  copyFace(currentFace, idleFace);
  enterFace();
  lastActivity = millis();
  resetButtons();
}

// Deep sleep: the Uno halts until Select is pressed, then needs a full hold.
void deepSleepUntilHold() {
  while (true) {
    noTone(BUZZER_PIN);
    waitForSelectRelease();

    byte adcsra = ADCSRA;
    ADCSRA &= ~(1 << ADEN);               // ADC off to save power
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    noInterrupts();
    sleep_enable();
    attachInterrupt(digitalPinToInterrupt(BTN_SELECT), wakeISR, LOW);
    interrupts();
    sleep_cpu();                           // sleeps here until Select goes LOW
    sleep_disable();
    detachInterrupt(digitalPinToInterrupt(BTN_SELECT));
    ADCSRA = adcsra;

    // Woke up: only power on if Select is held long enough.
    unsigned long t = millis();
    while (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - t >= HOLD_MS) {
        powerOn();
        return;
      }
    }
    // Released too early: go back to sleep.
  }
}

void powerOff() {
  LOG("SNTCH powering off");

  // Little goodbye: eyes droop and close, descending tones.
  clearScreen();
  copyFace(currentFace, idleFace);
  renderFace(currentFace, 0);
  beep(660, 100);
  tweenFace(halfFace, 280);
  beep(440, 100);
  tweenFace(closedFace, 280);
  beep(330, 160);
  delay(500);

  clearScreen();                  // black first, so nothing shows if the backlight stays on
  backlight(false);

  deepSleepUntilHold();   // returns after powerOn()
}

// =====================================================================
//  MENU
// =====================================================================
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  printCenteredF(F("SNTCH"), 8, 2);

  tft.setTextSize(1);
  for (uint8_t i = 0; i < NUM_ITEMS; i++) {
    int y = 34 + i * 16;
    if (i == selected) {
      tft.fillRect(MARGIN, y - 3, tft.width() - 2 * MARGIN, 14, ST77XX_WHITE);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(MARGIN + 6, y + 1);
    tft.print(menuItems[i]);
  }

  tft.setTextColor(ST77XX_WHITE);
  printCenteredF(F("< > Move   OK Open"), 128, 1);
  printCenteredF(F("Back = Home"), 140, 1);
  printCenteredF(F("Hold OK = Power off"), 152, 1);
}

void drawDetail() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);

  uint8_t size = fitSize(menuItems[selected], 2);
  printCentered(menuItems[selected], 24, size);

  printCenteredF(F("Screen opened."), 70, 1);
  printCenteredF(F("Back = return"), 86, 1);
}

// =====================================================================
//  SESSIONS  (saved in EEPROM, so they survive power-off)
// =====================================================================
const uint8_t MAX_PER_TYPE = 5;       // 5 activities + 5 exams
const uint8_t MAX_SESSIONS = 10;
const uint8_t EEPROM_MAGIC = 0x5A;

struct Session {
  char type;         // 0 = empty slot, 'A' = activity, 'E' = exam
  uint8_t num;       // 1..5, shown as "Activity 1"
  char code[6];      // join code (goes into the join QR)
};

Session sessions[MAX_SESSIONS];
uint8_t students[MAX_SESSIONS];       // live headcount (not saved)

char listType = 'A';                  // which list is open
int8_t listSel = 0;                   // highlighted row in the list
int8_t curSlot = -1;                  // session that is open
uint8_t actionSel = 0;                // 0 Join QR, 1 Students, 2 Delete
char qrKind = 'W';                    // 'W' = Wi-Fi QR, 'S' = session join QR

const char CODE_ALPHABET[] PROGMEM = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // 31 chars, no look-alikes

void saveSession(uint8_t i) { EEPROM.put(1 + i * sizeof(Session), sessions[i]); }

void loadSessions() {
  if (EEPROM.read(0) != EEPROM_MAGIC) {            // first run: start empty
    memset(sessions, 0, sizeof(sessions));
    for (uint8_t i = 0; i < MAX_SESSIONS; i++) saveSession(i);
    EEPROM.write(0, EEPROM_MAGIC);
  } else {
    EEPROM.get(1, sessions);
  }
}

uint8_t countType(char t) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) if (sessions[i].type == t) c++;
  return c;
}

// slot of the n-th session of a type (in slot order), or -1
int8_t slotOfRow(char t, uint8_t row) {
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].type == t) {
      if (row == 0) return i;
      row--;
    }
  }
  return -1;
}

uint8_t listRows() {
  uint8_t c = countType(listType);
  return c + (c < MAX_PER_TYPE ? 1 : 0);          // +1 for the "+ Add session" row
}

void sessionName(int8_t slot, char* out) {        // out needs 12 bytes
  const char* p = (sessions[slot].type == 'A') ? "Activity " : "Exam ";
  uint8_t n = 0;
  while (*p) out[n++] = *p++;
  out[n++] = '0' + sessions[slot].num;
  out[n] = 0;
}

void makeCode(char* out) {                        // 6 chars, unique among saved sessions
  randomSeed(analogRead(A0) ^ (uint16_t)micros());
  for (;;) {
    for (uint8_t i = 0; i < 6; i++) out[i] = pgm_read_byte(&CODE_ALPHABET[random(31)]);
    bool dup = false;
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
      if (sessions[i].type && memcmp(sessions[i].code, out, 6) == 0) dup = true;
    if (!dup) return;
  }
}

int8_t addSession(char t) {
  if (countType(t) >= MAX_PER_TYPE) return -1;
  int8_t slot = -1;
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) if (!sessions[i].type) { slot = i; break; }
  if (slot < 0) return -1;

  uint8_t num = 1;                                // lowest number not in use
  for (;;) {
    bool taken = false;
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
      if (sessions[i].type == t && sessions[i].num == num) taken = true;
    if (!taken) break;
    num++;
  }
  char code[6];
  makeCode(code);                                 // before the slot counts as "in use"
  sessions[slot].type = t;
  sessions[slot].num = num;
  memcpy(sessions[slot].code, code, 6);
  students[slot] = 0;
  saveSession(slot);
  return slot;
}

void deleteSession(int8_t slot) {
  memset(&sessions[slot], 0, sizeof(Session));
  students[slot] = 0;
  saveSession(slot);
}

// =====================================================================
//  SESSION SCREENS
// =====================================================================
void drawList() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  printCenteredF(listType == 'A' ? F("Activities") : F("Exams"), 8, 2);

  uint8_t cnt = countType(listType);
  uint8_t rows = listRows();
  tft.setTextSize(1);
  for (uint8_t i = 0; i < rows; i++) {
    int y = 34 + i * 16;
    if (i == listSel) {
      tft.fillRect(MARGIN, y - 3, tft.width() - 2 * MARGIN, 14, ST77XX_WHITE);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(MARGIN + 6, y + 1);
    if (i < cnt) {
      int8_t slot = slotOfRow(listType, i);
      char nm[12];
      sessionName(slot, nm);
      tft.print(nm);
      uint8_t n = students[slot];                 // headcount on the right
      int digits = (n >= 100) ? 3 : (n >= 10) ? 2 : 1;
      tft.setCursor(tft.width() - MARGIN - 6 - digits * 6, y + 1);
      tft.print(n);
    } else {
      tft.print(F("+ Add session"));
    }
  }

  tft.setTextColor(ST77XX_WHITE);
  if (cnt == 0) printCenteredF(F("No sessions yet"), 64, 1);
  if (cnt >= MAX_PER_TYPE) printCenteredF(F("List full (max 5)"), 116, 1);
  printCenteredF(F("< > Move   OK Open"), 140, 1);
  printCenteredF(F("Back = Menu"), 152, 1);
}

void drawSession() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);

  char nm[12];
  sessionName(curSlot, nm);
  printCentered(nm, 6, fitSize(nm, 2));

  char line[13] = "Code: ";
  memcpy(line + 6, sessions[curSlot].code, 6);
  line[12] = 0;
  printCentered(line, 28, 1);

  printCenteredF(F("Students in"), 44, 1);
  char num[4];
  itoa(students[curSlot], num, 10);
  printCentered(num, 56, 3);

  const char* const actions[3] = {"Show join QR", "Monitor students", "Delete session"};
  tft.setTextSize(1);
  for (uint8_t i = 0; i < 3; i++) {
    int y = 90 + i * 14;
    if (i == actionSel) {
      tft.fillRect(MARGIN, y - 2, tft.width() - 2 * MARGIN, 12, ST77XX_WHITE);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(MARGIN + 6, y);
    tft.print(actions[i]);
  }

  tft.setTextColor(ST77XX_WHITE);
  printCenteredF(F("< > Move   OK Select"), 140, 1);
  printCenteredF(F("Back = List"), 152, 1);
}

void drawStudentsCount() {
  tft.fillRect(0, 56, tft.width(), 42, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  char num[4];
  itoa(students[curSlot], num, 10);
  printCentered(num, 58, 5);
}

void drawStudents() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  char nm[12];
  sessionName(curSlot, nm);
  printCentered(nm, 8, fitSize(nm, 2));
  printCenteredF(F("Students joined"), 34, 1);
  drawStudentsCount();
  printCenteredF(F("Manual count (test)"), 108, 1);
  printCenteredF(F("< -1    +1 >"), 122, 1);
  printCenteredF(F("OK = reset to 0"), 136, 1);
  printCenteredF(F("Back = session"), 150, 1);
}

void drawConfirm() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  printCenteredF(F("Delete?"), 20, 2);
  char nm[12];
  sessionName(curSlot, nm);
  printCentered(nm, 50, fitSize(nm, 2));
  printCenteredF(F("Cannot be undone."), 80, 1);
  printCenteredF(F("OK = Yes, delete"), 126, 1);
  printCenteredF(F("Back = Cancel"), 140, 1);
}

// QR code: version 3 (29x29), low error correction, drawn black on white.
void drawQrScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);

  char text[48];
  char title[20];
  if (qrKind == 'W') {
    strcpy_P(text, PSTR("WIFI:T:WPA;S:" WIFI_SSID ";P:" WIFI_PASS ";;"));
    printCenteredF(F("Join Wi-Fi"), 4, 1);
  } else {
    strcpy_P(text, PSTR(JOIN_BASE));
    memcpy(text + strlen(text), sessions[curSlot].code, 6);
    text[strlen(JOIN_BASE) + 6] = 0;
    char nm[12];
    sessionName(curSlot, nm);
    strcpy_P(title, PSTR("Join "));
    strcat(title, nm);
    printCentered(title, 4, 1);
  }

  uint8_t qrBuf[110];
  QRCode qr;
  qrcode_initText(&qr, qrBuf, 3, ECC_LOW, text);

  const uint8_t QUIET = 3;
  uint8_t size = qr.size;
  uint8_t total = size + 2 * QUIET;
  uint8_t scale = 120 / total;
  if (scale < 1) scale = 1;
  int px = total * scale;
  int x0 = (tft.width() - px) / 2;
  int y0 = 16;
  tft.fillRect(x0, y0, px, px, ST77XX_WHITE);
  for (uint8_t y = 0; y < size; y++) {
    uint8_t x = 0;
    while (x < size) {
      if (!qrcode_getModule(&qr, x, y)) { x++; continue; }
      uint8_t run = x;
      while (run < size && qrcode_getModule(&qr, run, y)) run++;
      tft.fillRect(x0 + (x + QUIET) * scale, y0 + (y + QUIET) * scale,
                   (run - x) * scale, scale, ST77XX_BLACK);
      x = run;
    }
  }

  if (qrKind == 'W') {
    printCenteredF(F(WIFI_SSID), 140, 1);
    printCenteredF(F("Back = Menu"), 150, 1);
  } else {
    char line[13] = "Code: ";
    memcpy(line + 6, sessions[curSlot].code, 6);
    line[12] = 0;
    printCentered(line, 140, 1);
    printCenteredF(F("Back = Session"), 150, 1);
  }
}

// =====================================================================
//  BUTTON ACTIONS
// =====================================================================
void printState() {
#if DEBUG_SERIAL
  Serial.print(F("Screen: "));
  Serial.println((int)screen);
#endif
}

void openList(char t) {
  listType = t;
  listSel = 0;
  screen = SCREEN_LIST;
  drawList();
}

void onListSelect() {
  uint8_t cnt = countType(listType);
  if (listSel < cnt) {
    curSlot = slotOfRow(listType, listSel);
  } else {
    curSlot = addSession(listType);               // "+ Add session"
    if (curSlot < 0) return;
    beep(1047, 80);
  }
  actionSel = 0;
  screen = SCREEN_SESSION;
  drawSession();
}

void onSessionSelect() {
  if (actionSel == 0) {
    qrKind = 'S';
    screen = SCREEN_QR;
    drawQrScreen();
  } else if (actionSel == 1) {
    screen = SCREEN_STUDENTS;
    drawStudents();
  } else {
    screen = SCREEN_CONFIRM;
    drawConfirm();
  }
}

void onConfirmDelete() {
  deleteSession(curSlot);
  curSlot = -1;
  beep(300, 150);
  uint8_t rows = listRows();
  if (listSel >= rows) listSel = rows - 1;
  screen = SCREEN_LIST;
  drawList();
}

// Short press of Select (fires on release, after a hold was ruled out).
void onSelectShort() {
  lastActivity = millis();

  if (screen == SCREEN_SLEEP) {
    wakeUp();
    printState();
    return;
  }

  LOG("SELECT");
  beep(900, 40);
  switch (screen) {
    case SCREEN_FACE:
      screen = SCREEN_MENU;
      drawMenu();
      break;
    case SCREEN_MENU:
      if (selected == ITEM_ACTIVITIES)  openList('A');
      else if (selected == ITEM_EXAMS)  openList('E');
      else if (selected == ITEM_QR) {
        qrKind = 'W';
        screen = SCREEN_QR;
        drawQrScreen();
      } else {
        screen = SCREEN_DETAIL;
        drawDetail();
      }
      break;
    case SCREEN_LIST:     onListSelect();    break;
    case SCREEN_SESSION:  onSessionSelect(); break;
    case SCREEN_CONFIRM:  onConfirmDelete(); break;
    case SCREEN_STUDENTS:
      students[curSlot] = 0;
      drawStudentsCount();
      break;
    default: break;
  }
  printState();
}

void goBack() {
  switch (screen) {
    case SCREEN_MENU:
      enterFace();
      break;
    case SCREEN_DETAIL:
    case SCREEN_LIST:
      screen = SCREEN_MENU;
      drawMenu();
      break;
    case SCREEN_QR:
      if (qrKind == 'S') { screen = SCREEN_SESSION; drawSession(); }
      else               { screen = SCREEN_MENU;    drawMenu(); }
      break;
    case SCREEN_SESSION:
      screen = SCREEN_LIST;
      drawList();
      break;
    case SCREEN_STUDENTS:
    case SCREEN_CONFIRM:
      screen = SCREEN_SESSION;
      drawSession();
      break;
    default: break;
  }
}

void move(int8_t dir) {                           // -1 = Left, +1 = Right
  switch (screen) {
    case SCREEN_MENU:
      selected = (selected + NUM_ITEMS + dir) % NUM_ITEMS;
      drawMenu();
      break;
    case SCREEN_LIST: {
      uint8_t rows = listRows();
      listSel = (listSel + rows + dir) % rows;
      drawList();
      break;
    }
    case SCREEN_SESSION:
      actionSel = (actionSel + 3 + dir) % 3;
      drawSession();
      break;
    case SCREEN_STUDENTS:
      if (dir > 0 && students[curSlot] < 250) students[curSlot]++;
      if (dir < 0 && students[curSlot] > 0)   students[curSlot]--;
      drawStudentsCount();
      break;
    default: break;
  }
}

void onPress(uint8_t id) {   // 1 = Back, 2 = Left, 3 = Right
  lastActivity = millis();

  if (screen == SCREEN_SLEEP) {
    wakeUp();
    printState();
    return;
  }

  if (id == 1) {
    LOG("BACK");
    beep(400, 40);
    goBack();
  } else {
    LOG("LEFT/RIGHT");
    beep(id == 2 ? 650 : 750, 30);
    move(id == 2 ? -1 : 1);
  }
  printState();
}

void updateButtons(unsigned long now) {
  for (uint8_t i = 0; i < 4; i++) {
    bool reading = digitalRead(btns[i].pin);
    if (reading != btns[i].lastReading) {
      btns[i].changedAt = now;
      btns[i].lastReading = reading;
    }
    if (now - btns[i].changedAt > 30 && reading != btns[i].stable) {
      btns[i].stable = reading;
      if (i == 0) {                       // Select
        if (reading == LOW) {
          selectDown = true;
          selectHandled = false;
          selectDownAt = now;
          lastActivity = now;
        } else {
          if (selectDown && !selectHandled) onSelectShort();
          selectDown = false;
        }
      } else if (reading == LOW) {
        onPress(i);
      }
    }
  }
}

// Called every loop: has Select been held long enough to power off?
void checkPowerHold(unsigned long now) {
  if (selectDown && !selectHandled && now - selectDownAt >= HOLD_MS) {
    selectHandled = true;
    powerOff();            // returns after SNTCH is powered back on
  }
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
#if DEBUG_SERIAL
  Serial.begin(9600);
#endif
  loadSessions();

  for (uint8_t i = 0; i < 4; i++) pinMode(btns[i].pin, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
#if USE_BACKLIGHT_PIN
  pinMode(BACKLIGHT_PIN, OUTPUT);
#endif
  backlight(true);

  tft.initR(TFT_TAB);
  tft.setRotation(0);
  clearScreen();

  randomSeed(analogRead(A0));

  // Hold Select while plugging in to skip the intro.
  bool skipIntro = (digitalRead(BTN_SELECT) == LOW);
  waitForSelectRelease();
  if (!skipIntro) playIntro();

  copyFace(currentFace, idleFace);
  enterFace();
  lastActivity = millis();
  resetButtons();
  LOG("SNTCH ready");
}

unsigned long idleLimitFor(uint8_t sc) {   // uint8_t, not Screen: the IDE's auto-prototype would come before the enum
  if (sc == SCREEN_QR || sc == SCREEN_STUDENTS) return QR_TIMEOUT;
  if (sc == SCREEN_LIST || sc == SCREEN_SESSION || sc == SCREEN_CONFIRM) return SESSION_TIMEOUT;
  return SLEEP_TIMEOUT;
}

void loop() {
  unsigned long now = millis();

  updateButtons(now);
  checkPowerHold(now);

  if (screen != SCREEN_SLEEP && now - lastActivity >= idleLimitFor(screen)) {
    enterSleep();
  }

  if (screen == SCREEN_FACE) {
    updateFace(now);
  } else if (screen == SCREEN_SLEEP) {
    updateSleep(now);
  }
}