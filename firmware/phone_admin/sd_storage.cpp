#include "sd_storage.h"

#include <Arduino.h>
#include <PanelPins.h>
#include <SD.h>
#include <SPI.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sntch_fw {
namespace {

constexpr char StageSuffix[] = ".sntch-new";
constexpr char BackupSuffix[] = ".sntch-old";

bool endsWith(const std::string& path, const char* suffix) {
  const size_t length = std::strlen(suffix);
  return path.size() >= length && path.compare(path.size() - length, length, suffix) == 0;
}

}  // namespace

SdStorage::SdStorage(const char* mountPoint)
    : mountPoint_(mountPoint), root_(mountPoint_ + "/sntch") {}

std::string SdStorage::diskPath(const std::string& path) const {
  return path == "/" ? root_ : root_ + path;
}

bool SdStorage::begin() {
  healthy_ = false;
  if (!SD.begin(panel::SoloSdCs, SPI, 4000000, mountPoint_.c_str(), 5, false)) {
    Serial.println("[sntch] microSD mount failed; card was not formatted");
    return false;
  }

  struct stat info {};
  if (stat(root_.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
    Serial.println("[sntch] missing /sntch directory on the microSD card");
    SD.end();
    return false;
  }

  healthy_ = true;
  if (!recoverWrites()) {
    SD.end();
    return false;
  }
  Serial.println("[sntch] microSD classroom storage mounted");
  return true;
}

bool SdStorage::fail(const char* operation, const std::string& path) {
  const int error = errno;
  Serial.printf("[sntch] microSD %s failed: %s (errno %d)\n", operation, path.c_str(), error);
  healthy_ = false;
  return false;
}

bool SdStorage::exists(const std::string& path) {
  if (!healthy_) return false;
  struct stat info {};
  if (stat(diskPath(path).c_str(), &info) == 0) {
    if (S_ISREG(info.st_mode)) return true;
    errno = EISDIR;
    return fail("expected a file", path);
  }
  return errno == ENOENT ? false : fail("stat", path);
}

std::string SdStorage::read(const std::string& path) {
  if (!healthy_) return {};
  FILE* file = std::fopen(diskPath(path).c_str(), "rb");
  if (!file) {
    if (errno != ENOENT) fail("open for read", path);
    return {};
  }

  std::string data;
  char buffer[256];
  size_t count;
  while ((count = std::fread(buffer, 1, sizeof buffer, file)) != 0) {
    data.append(buffer, count);
  }
  const bool readError = std::ferror(file) != 0;
  if (std::fclose(file) != 0 || readError) {
    fail("read", path);
    return {};
  }
  return data;
}

bool SdStorage::ensureParent(const std::string& path) {
  // The classroom core uses only one level of subdirectories (sub, att, del).
  const size_t slash = path.rfind('/');
  if (slash == 0) return true;
  const std::string parent = diskPath(path.substr(0, slash));
  if (mkdir(parent.c_str(), 0777) == 0) return true;
  if (errno == EEXIST) {
    struct stat info {};
    if (stat(parent.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) return true;
  }
  return fail("create directory", parent);
}

bool SdStorage::write(const std::string& path, const std::string& data) {
  if (!healthy_ || !ensureParent(path)) return false;
  FILE* file = std::fopen(diskPath(path).c_str(), "wb");
  if (!file) return fail("open for write", path);
  const bool synced = std::fwrite(data.data(), 1, data.size(), file) == data.size() &&
                      std::fflush(file) == 0 && fsync(fileno(file)) == 0;
  const bool closed = std::fclose(file) == 0;
  return (synced && closed) || fail("write or sync", path);
}

bool SdStorage::writeAtomic(const std::string& path, const std::string& data) {
  const std::string stage = path + StageSuffix;
  const std::string backup = path + BackupSuffix;
  if (!write(stage, data)) return false;

  // FAT cannot atomically rename over an existing file. Keep the old record
  // until the staged record is installed. Boot recovery resolves either gap.
  const bool hadOriginal = exists(path);
  if (!healthy_) return false;
  if (hadOriginal && std::rename(diskPath(path).c_str(), diskPath(backup).c_str()) != 0)
    return fail("back up file", path);
  if (std::rename(diskPath(stage).c_str(), diskPath(path).c_str()) != 0)
    return fail("install staged file", path);

  // The new record is already committed. If cleanup fails, stop further I/O
  // and let recovery discard the backup on the next boot.
  if (hadOriginal) remove(backup);
  return true;
}

bool SdStorage::remove(const std::string& path) {
  if (!healthy_) return false;
  if (unlink(diskPath(path).c_str()) == 0 || errno == ENOENT) return true;
  return fail("remove", path);
}

void SdStorage::collectFiles(const std::string& dir, std::vector<std::string>& files) {
  DIR* folder = opendir(diskPath(dir).c_str());
  if (!folder) {
    if (errno != ENOENT || dir == "/") fail("open directory", dir);
    return;
  }

  while (healthy_) {
    errno = 0;
    const dirent* entry = readdir(folder);
    if (!entry) {
      if (errno) fail("read directory", dir);
      break;
    }
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    const std::string path = (dir == "/" ? dir : dir + "/") + entry->d_name;
    struct stat info {};
    if (stat(diskPath(path).c_str(), &info) != 0) fail("stat directory entry", path);
    else if (S_ISDIR(info.st_mode)) collectFiles(path, files);
    else files.push_back(path);
  }
  closedir(folder);
}

std::vector<std::string> SdStorage::list(const std::string& dir) {
  std::vector<std::string> files;
  if (healthy_) collectFiles(dir, files);
  return files;
}

bool SdStorage::recoverWrites() {
  const auto files = list("/");
  if (!healthy_) return false;
  for (const auto& path : files) {
    if (endsWith(path, BackupSuffix)) {
      const std::string target = path.substr(0, path.size() - sizeof BackupSuffix + 1);
      if (exists(target)) {
        if (!remove(path)) return false;
      } else {
        if (!healthy_) return false;
        if (std::rename(diskPath(path).c_str(), diskPath(target).c_str()) != 0)
          return fail("restore backup", target);
      }
    } else if (endsWith(path, StageSuffix) && !remove(path)) {
      return false;
    }
  }
  return true;
}

uint64_t SdStorage::usedBytes() { return healthy_ ? SD.usedBytes() : 0; }
uint64_t SdStorage::totalBytes() { return healthy_ ? SD.totalBytes() : 0; }

}  // namespace sntch_fw
