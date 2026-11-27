// Build-time configuration for the SNTCH controller firmware.
// Every value can be overridden with a -D build flag; see docs/FIRMWARE.md.
#pragma once

#ifndef SNTCH_FIRMWARE_VERSION
#define SNTCH_FIRMWARE_VERSION "0.1.0"
#endif

// Access point. Supply a private value through SNTCH_AP_PASSWORD at build time.
#ifndef SNTCH_AP_SSID
#define SNTCH_AP_SSID "SNTCH-EXAM"
#endif
#ifndef SNTCH_AP_PASSWORD
#error "Set SNTCH_AP_PASSWORD to a private WPA2 password in the build environment."
#endif
static_assert(sizeof(SNTCH_AP_PASSWORD) >= 9, "SNTCH_AP_PASSWORD must be at least 8 characters.");
#ifndef SNTCH_AP_CHANNEL
#define SNTCH_AP_CHANNEL 1
#endif

// The ESP32 access point supports up to 10 associated stations. This is the
// hard limit behind the "50 students" data model; see docs/FIRMWARE.md.
#ifndef SNTCH_AP_MAX_CLIENTS
#define SNTCH_AP_MAX_CLIENTS 10
#endif

// Teacher sign-in PIN. Supply a private value at build time; never log it.
#ifndef SNTCH_TEACHER_PIN
#error "Set SNTCH_TEACHER_PIN to a private PIN in the build environment."
#endif
static_assert(sizeof(SNTCH_TEACHER_PIN) >= 9, "SNTCH_TEACHER_PIN must be at least 8 characters.");
constexpr bool sntchTeacherPinIsDigitsOnly(const char* pin) {
  return *pin == '\0' || ((*pin >= '0' && *pin <= '9') && sntchTeacherPinIsDigitsOnly(pin + 1));
}
static_assert(sntchTeacherPinIsDigitsOnly(SNTCH_TEACHER_PIN), "SNTCH_TEACHER_PIN must use digits only.");

#ifndef SNTCH_HTTP_PORT
#define SNTCH_HTTP_PORT 80
#endif

#ifndef SNTCH_SERIAL_BAUD
#define SNTCH_SERIAL_BAUD 115200
#endif

// Periodic state snapshot (exam clocks are checkpointed this often).
#ifndef SNTCH_SAVE_INTERVAL_MS
#define SNTCH_SAVE_INTERVAL_MS 60000UL
#endif

// Serial diagnostics cadence; keep it slow to avoid log spam.
#ifndef SNTCH_DIAG_INTERVAL_MS
#define SNTCH_DIAG_INTERVAL_MS 30000UL
#endif

#ifndef SNTCH_AP_HOSTNAME
#define SNTCH_AP_HOSTNAME "sntch"
#endif

#ifndef SNTCH_PHONE_ADMIN
#define SNTCH_PHONE_ADMIN 0
#endif

// Optional read-only status link to an Arduino Uno (or any 5 V UART display
// board). The ESP32 only transmits; the Uno never sends commands back.
#ifndef SNTCH_UNO_STATUS
#define SNTCH_UNO_STATUS 1
#endif
#ifndef SNTCH_UNO_BAUD
#define SNTCH_UNO_BAUD 9600
#endif
#ifndef SNTCH_UNO_TX_PIN
#define SNTCH_UNO_TX_PIN 17
#endif
#ifndef SNTCH_UNO_RX_PIN
#define SNTCH_UNO_RX_PIN 16  // reserved; do not connect the Uno TX
#endif
#ifndef SNTCH_UNO_STATUS_INTERVAL_MS
#define SNTCH_UNO_STATUS_INTERVAL_MS 2000UL
#endif
