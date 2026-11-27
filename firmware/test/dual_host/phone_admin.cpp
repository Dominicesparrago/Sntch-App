#include <Arduino.h>
#include <SD.h>
#include <filesystem>
#include <fstream>

#include "phone_admin_panel.h"
#include "sd_storage.h"
#include "sntch/api.hpp"

using namespace panel;
using namespace sntch;
using namespace sntch_fw;
namespace fs = std::filesystem;

uint32_t fakeNow = 1000;
std::array<uint8_t, 40> pinLevels{}, pinModes{};
bool fakeTftInit = false;
SPIClass SPI;
FakeSerial Serial;
FakeSd SD;

static Json parse(const char* text) {
  Json doc;
  assert(parseJson(text, doc));
  return doc;
}

static unsigned liveActivity(Classroom& room) {
  unsigned item = 0, session = 0;
  assert(room.createActivity(parse(R"({"title":"Activity","target":"9-A"})"), item).ok);
  assert(room.publishItem("activity", item).ok);
  Json request = parse(R"({"refType":"activity"})");
  request.set("refId", Json(static_cast<int>(item)));
  assert(room.createSession(request, fakeNow, session).ok);
  assert(room.sessionAction(session, "start", fakeNow).ok);
  return session;
}

static void display(PhoneAdminPanel& panel, Classroom& room) {
  const auto& data = panel.displayState();
  auto tick = [&](bool wifi, bool storage) {
    fakeNow += 300;
    panel.poll(room, wifi, wifi, storage, 1, 0, "192.168.4.1", "SNTCH-EXAM");
  };
  tick(true, true);
  assert(data.kind == 0 && qrSide(data.side) && std::string(data.address) == "192.168.4.1");
  const auto session = liveActivity(room);
  tick(true, true);
  assert(data.kind == 1 && qrSide(data.side) && data.code == room.sessions()[0].code);
  const auto second = liveActivity(room);
  tick(true, true);
  assert(data.kind == 0 && qrSide(data.side));
  assert(room.sessionAction(second, "end", fakeNow).ok);
  tick(true, true);
  assert(data.kind == 1 && data.code == room.sessions()[0].code);
  assert(room.sessionAction(session, "pause", fakeNow).ok);
  tick(true, true);
  assert(data.kind == 0 && qrSide(data.side));
  tick(false, true);
  assert(!data.side && data.fault == Offline);
  tick(true, false);
  assert(!data.side && data.fault == panel_ui::MicroSdUnavailable);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const fs::path mount = argv[1], root = mount / "sntch";
  PhoneAdminPanel panel;
  panel.begin();
  assert(SPI.began && fakeTftInit);
  for (auto pin : {SoloUp, SoloDown, SoloMenu, SoloSelect}) assert(pinModes[pin] == 0);

  SdStorage storage(argv[1]);
  SD.present = false;
  assert(!storage.begin() && !storage.healthy());
  SD.present = true;
  assert(!storage.begin() && !fs::exists(root));
  fs::create_directory(root);
  assert(storage.begin());
  Classroom blank(&storage, "58372649");
  assert(!blank.load() && !storage.exists("/state.json"));
  assert(storage.write("/sntch_pending_init.txt", "SNTCH-PENDING-INIT-V1"));
  Classroom room(&storage, "58372649");
  assert(room.load() && storage.exists("/state.json"));
  display(panel, room);

  const auto state = root / "state.json";
  // Interruption after the old file was moved but before the replacement.
  fs::rename(state, root / "state.json.sntch-old");
  std::ofstream(root / "state.json.sntch-new") << "partial";
  assert(storage.begin() && !fs::exists(root / "state.json.sntch-new"));
  Classroom reloaded(&storage, "58372649");
  assert(reloaded.load() && reloaded.sessions().size() == 2);

  // Interruption after installing the replacement but before cleanup.
  fs::copy_file(state, root / "state.json.sntch-old");
  assert(storage.begin() && !fs::exists(root / "state.json.sntch-old"));
  assert(storage.writeAtomic("/sub/1.json", "first"));
  assert(storage.writeAtomic("/sub/1.json", "replacement"));
  assert(storage.read("/sub/1.json") == "replacement");

  DeviceInfo device{};
  device.storageTotal = storage.totalBytes();
  device.storageUsed = storage.usedBytes();
  Api api(&reloaded, device);
  Request health;
  health.method = "GET";
  health.path = "/api/health";
  const auto response = api.handle(health);
  const auto report = parse(response.body.c_str());
  assert(response.status == 200 && report.num("storageTotal") == (32ULL << 30));
  assert(report.num("storageUsed") == (3ULL << 30));

  // Failed staging must preserve the current state and stop further writes.
  fs::create_directory(root / "state.json.sntch-new");
  assert(!storage.writeAtomic("/state.json", "replacement") && !storage.healthy());
  assert(!storage.writeAtomic("/sub/1.json", "blocked") && fs::exists(state));
  std::puts("Option C auto-start QR, microSD recovery and capacity checks passed");
}
