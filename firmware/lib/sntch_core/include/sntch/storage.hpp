// Storage abstraction: the classroom core never talks to LittleFS directly, so
// the same logic runs on supported ESP32 targets and in host tests/simulator.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sntch {

class Storage {
 public:
  virtual ~Storage() {}
  virtual bool begin() { return true; }
  // Empty string means "missing".
  virtual std::string read(const std::string& path) = 0;
  virtual bool exists(const std::string& path) { return !read(path).empty(); }
  virtual bool write(const std::string& path, const std::string& data) = 0;
  // Stage to <path>.tmp then rename, so a reset mid-write cannot tear the file.
  virtual bool writeAtomic(const std::string& path, const std::string& data) {
    return write(path + ".tmp", data) && write(path, data) && remove(path + ".tmp");
  }
  virtual bool remove(const std::string& path) = 0;
  virtual std::vector<std::string> list(const std::string& dir) = 0;
  virtual size_t usedBytes() { return 0; }
  virtual size_t totalBytes() { return 0; }
};

// In-memory storage with a byte budget. Used by unit tests and the host
// simulator's fast mode; it can simulate a full disk deterministically.
class MemoryStorage : public Storage {
 public:
  explicit MemoryStorage(size_t capacity = 1024 * 1024) : capacity_(capacity), used_(0), failWrites_(false) {}

  std::string read(const std::string& path) {
    std::map<std::string, std::string>::const_iterator it = files_.find(path);
    return it == files_.end() ? std::string() : it->second;
  }

  bool exists(const std::string& path) { return files_.find(path) != files_.end(); }

  bool write(const std::string& path, const std::string& data) {
    if (failWrites_) return false;
    size_t old = files_[path].size();
    if (used_ - old + data.size() > capacity_) return false;
    used_ = used_ - old + data.size();
    files_[path] = data;
    return true;
  }

  bool remove(const std::string& path) {
    std::map<std::string, std::string>::iterator it = files_.find(path);
    if (it == files_.end()) return true;
    used_ -= it->second.size();
    files_.erase(it);
    return true;
  }

  std::vector<std::string> list(const std::string& dir) {
    std::vector<std::string> out;
    std::string prefix = dir;
    if (!prefix.empty() && prefix[prefix.size() - 1] != '/') prefix += "/";
    for (std::map<std::string, std::string>::const_iterator it = files_.begin(); it != files_.end(); ++it) {
      if (it->first.compare(0, prefix.size(), prefix) == 0) out.push_back(it->first);
    }
    return out;
  }

  size_t usedBytes() { return used_; }
  size_t totalBytes() { return capacity_; }

  void setFailWrites(bool fail) { failWrites_ = fail; }
  void setCapacity(size_t bytes) { capacity_ = bytes; }
  size_t fileCount() const { return files_.size(); }

 private:
  std::map<std::string, std::string> files_;
  size_t capacity_;
  size_t used_;
  bool failWrites_;
};

}  // namespace sntch
