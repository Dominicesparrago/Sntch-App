#include "phone_admin_panel.h"

#include <SPI.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>
#include "uptime.h"

namespace sntch_fw {
namespace {
constexpr uint32_t BootMs = 1000, StatusMs = 250;
}  // namespace

using namespace panel;
using namespace panel_ui;

PhoneAdminPanel::PhoneAdminPanel()
    : tft(&SPI, SoloTftCs, SoloTftDc, SoloTftReset), ui(tft, data) {}

void PhoneAdminPanel::begin() {
  // Both peripherals must be deselected before either starts using the bus.
  for (const uint8_t pin : {SoloTftCs, SoloSdCs}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  SPI.begin(SoloSck, SoloSdMiso, SoloMosi, SoloTftCs);
  tft.initR(SNTCH_PANEL_TFT_INIT);
  tft.setRotation(Rotation);
  tft.setSPISpeed(8000000);
  data.linked = true;
  offers.seed(esp_random());
  started = millis();
  data.faceSince = started;
  lastStatus = started - StatusMs;
  for (uint8_t row = 0; row < Height; ++row) ui.tick(started);
}

void PhoneAdminPanel::update(sntch::Classroom& room, bool wifi, bool service, bool storage,
                             const char* ip, const char* ssid, uint32_t now) {
  const auto time = uptimeMs();
  const bool ready = wifi && service && storage && !room.storageError();
  const uint8_t flags = room.hasTeacherLogin(time) ? TeacherLogin : 0;
  if (flags != data.flags) {
    data.flags = flags;
    ui.refresh();
  }

  char address[sizeof data.address];
  if (SNTCH_HTTP_PORT == 80 || !*ip)
    std::snprintf(address, sizeof address, "%s", ip);
  else
    std::snprintf(address, sizeof address, "%s:%u", ip, unsigned(SNTCH_HTTP_PORT));
  if (std::strcmp(data.address, address) != 0) {
    std::snprintf(data.address, sizeof data.address, "%s", address);
    ui.refresh();
  }
  std::snprintf(data.ssid, sizeof data.ssid, "%s", ssid);

  // The storage/service state takes precedence; never keep displaying a stale
  // join code. For zero or multiple live activities, show controller pairing.
  uint8_t kind = ready ? 1 : 0;
  uint8_t fault = Offline;
  if (ready) {
    fault = offers.refresh(room, time, true, ip, kind, false, 0, 0);
    if (fault) {
      kind = 0;
      fault = offers.refresh(room, time, true, ip, kind, false, 0, 0);
    }
  } else {
    offers.disconnected();
    if (!storage || room.storageError()) fault = MicroSdUnavailable;
  }

  if (now - started < BootMs && !ready) return;
  if (fault != data.fault || kind != data.kind || (!fault && shownRevision != offers.revision)) {
    data.kind = kind;
    data.fault = fault;
    data.side = fault ? 0 : offers.size;
    if (!fault) {
      data.qrSince = now;
      shownRevision = offers.revision;
      std::memcpy(data.code, offers.label, sizeof data.code);
      std::memcpy(data.matrix, offers.matrix, offers.length);
    }
    ui.refresh();
  }
  const auto scene = ui.state();
  if (scene.view != QrView || scene.index != kind) ui.set({QrView, kind, Idle}, now);
}

void PhoneAdminPanel::poll(sntch::Classroom& room, bool wifi, bool service, bool storage,
                           uint8_t, uint8_t, const char* ip, const char* ssid) {
  const uint32_t now = millis();
  if (now - lastStatus >= StatusMs || (!storage && data.side)) {
    lastStatus = now;
    update(room, wifi, service, storage, ip, ssid, now);
  }
  const uint32_t frameStart = micros();
  for (uint8_t i = 0; i < 8 && micros() - frameStart < 1800; ++i) ui.tick(now);
}

}  // namespace sntch_fw
