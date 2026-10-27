#include "littlefs_storage.h"

#include <Arduino.h>

namespace sntch_fw {
namespace {

void collectFiles(File directory, std::vector<std::string>& out) {
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory()) {
      collectFiles(entry, out);
    } else {
      std::string path = entry.path();
      if (!path.empty() && path[0] != '/') path.insert(path.begin(), '/');
      out.push_back(path);
    }
    entry.close();
  }
}

}  // namespace

bool LittleFsStorage::begin() {
  // Never format automatically: a mount failure cannot distinguish a blank
  // partition from corruption or a partition-layout mismatch.
  if (!LittleFS.begin(false)) {
    Serial.println("[sntch] ERROR: LittleFS mount failed; filesystem was not formatted");
    return false;
  }
  if ((!LittleFS.exists("/sub") && !LittleFS.mkdir("/sub")) ||
      (!LittleFS.exists("/att") && !LittleFS.mkdir("/att"))) {
    Serial.println("[sntch] ERROR: LittleFS mounted but required directories could not be created");
    LittleFS.end();
    return false;
  }
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

bool LittleFsStorage::exists(const std::string& path) {
  return LittleFS.exists(path.c_str());
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
  collectFiles(root, out);
  root.close();
  return out;
}

size_t LittleFsStorage::usedBytes() { return LittleFS.usedBytes(); }
size_t LittleFsStorage::totalBytes() { return LittleFS.totalBytes(); }

}  // namespace sntch_fw
