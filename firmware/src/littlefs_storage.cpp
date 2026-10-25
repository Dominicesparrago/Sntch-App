#include "littlefs_storage.h"

#include <Arduino.h>

namespace sntch_fw {

bool LittleFsStorage::begin() {
  // Format on first boot / after a filesystem change so a fresh board comes up
  // usable without a manual uploadfs step.
  if (!LittleFS.begin(true)) {
    Serial.println("[sntch] LittleFS mount failed");
    return false;
  }
  if (!LittleFS.exists("/sub")) LittleFS.mkdir("/sub");
  if (!LittleFS.exists("/att")) LittleFS.mkdir("/att");
  Serial.printf("[sntch] LittleFS mounted: %u/%u bytes used\n",
                static_cast<unsigned>(LittleFS.usedBytes()),
                static_cast<unsigned>(LittleFS.totalBytes()));
  return true;
}

bool LittleFsStorage::ensureParent(const std::string& path) {
  size_t slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) return true;
  String dir(path.substr(0, slash).c_str());
  if (!LittleFS.exists(dir)) return LittleFS.mkdir(dir);
  return true;
}

std::string LittleFsStorage::read(const std::string& path) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file) return std::string();
  std::string data;
  data.reserve(file.size());
  uint8_t buffer[256];
  while (file.available()) {
    int n = file.read(buffer, sizeof(buffer));
    if (n <= 0) break;
    data.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(n));
  }
  file.close();
  return data;
}

bool LittleFsStorage::write(const std::string& path, const std::string& data) {
  if (!ensureParent(path)) return false;
  File file = LittleFS.open(path.c_str(), "w");
  if (!file) return false;
  size_t written = file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  file.flush();
  file.close();
  return written == data.size();
}

bool LittleFsStorage::writeAtomic(const std::string& path, const std::string& data) {
  const std::string temp = path + ".tmp";
  if (!write(temp, data)) {
    remove(temp);
    return false;
  }
  if (LittleFS.exists(path.c_str())) LittleFS.remove(path.c_str());
  if (LittleFS.rename(temp.c_str(), path.c_str())) return true;
  // Rename can fail on some flash states; fall back to a direct write so the
  // record is not lost, accepting a small torn-write window in that case.
  bool ok = write(path, data);
  remove(temp);
  return ok;
}

bool LittleFsStorage::remove(const std::string& path) {
  if (!LittleFS.exists(path.c_str())) return true;
  return LittleFS.remove(path.c_str());
}

std::vector<std::string> LittleFsStorage::list(const std::string& dir) {
  std::vector<std::string> out;
  File root = LittleFS.open(dir.c_str());
  if (!root || !root.isDirectory()) return out;
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    out.push_back(file.path());
    file.close();
  }
  return out;
}

size_t LittleFsStorage::usedBytes() { return LittleFS.usedBytes(); }
size_t LittleFsStorage::totalBytes() { return LittleFS.totalBytes(); }

}  // namespace sntch_fw
