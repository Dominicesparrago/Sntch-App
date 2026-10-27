#pragma once

#include <LittleFS.h>

#include "sntch/storage.hpp"

namespace sntch_fw {

// Storage on ESP32 internal flash. Writes are staged to <path>.tmp and
// renamed, matching the documented recovery behaviour.
class LittleFsStorage : public sntch::Storage {
 public:
  bool begin() override;
  std::string read(const std::string& path) override;
  bool exists(const std::string& path) override;
  bool write(const std::string& path, const std::string& data) override;
  bool writeAtomic(const std::string& path, const std::string& data) override;
  bool remove(const std::string& path) override;
  std::vector<std::string> list(const std::string& dir) override;
  size_t usedBytes() override;
  size_t totalBytes() override;

 private:
  static bool ensureParent(const std::string& path);
};

}  // namespace sntch_fw
