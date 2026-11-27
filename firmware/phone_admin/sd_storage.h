#pragma once

#include "sntch/storage.hpp"

namespace sntch_fw {

class SdStorage : public sntch::Storage {
 public:
  explicit SdStorage(const char* mountPoint = "/sd");
  bool begin() override;
  bool healthy() const { return healthy_; }
  std::string read(const std::string& path) override;
  bool exists(const std::string& path) override;
  bool write(const std::string& path, const std::string& data) override;
  bool writeAtomic(const std::string& path, const std::string& data) override;
  bool remove(const std::string& path) override;
  std::vector<std::string> list(const std::string& dir) override;
  uint64_t usedBytes() override;
  uint64_t totalBytes() override;

 private:
  const std::string mountPoint_;
  const std::string root_;
  bool healthy_ = false;
  std::string diskPath(const std::string& path) const;
  bool fail(const char* operation, const std::string& path);
  bool ensureParent(const std::string& path);
  void collectFiles(const std::string& dir, std::vector<std::string>& files);
  bool recoverWrites();
};

}  // namespace sntch_fw
