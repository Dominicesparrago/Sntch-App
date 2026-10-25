// SNTCH host simulator: serves the exact firmware core over real HTTP so the
// Android app and the end-to-end test can talk to a controller without hardware.
//
//   host_sim [port] [data-dir]
//
// It is a verification harness, not a shippable server: single-threaded, one
// connection at a time, no Wi-Fi access point.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#endif

#include "sntch/api.hpp"
#include "sntch/classroom.hpp"
#include "sntch/storage.hpp"

namespace fs = std::filesystem;
using namespace sntch;

namespace {

class FileStorage : public Storage {
 public:
  explicit FileStorage(const std::string& root) : root_(root) {}

  bool begin() {
    std::error_code ec;
    fs::create_directories(path("/sub"), ec);
    fs::create_directories(path("/att"), ec);
    return !ec;
  }

  std::string read(const std::string& p) {
    std::ifstream in(path(p), std::ios::binary);
    if (!in) return std::string();
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data;
  }

  bool write(const std::string& p, const std::string& data) {
    std::string full = path(p);
    fs::create_directories(fs::path(full).parent_path());
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    return out.good();
  }

  bool writeAtomic(const std::string& p, const std::string& data) {
    if (!write(p + ".tmp", data)) return false;
    std::error_code ec;
    fs::remove(path(p), ec);
    fs::rename(path(p + ".tmp"), path(p), ec);
    if (ec) {
      // Fall back to a plain write rather than losing the record.
      return write(p, data) && remove(p + ".tmp");
    }
    return true;
  }

  bool remove(const std::string& p) {
    std::error_code ec;
    fs::remove(path(p), ec);
    return true;
  }

  std::vector<std::string> list(const std::string& dir) {
    std::vector<std::string> out;
    std::string prefix = dir;
    if (!prefix.empty() && prefix[prefix.size() - 1] != '/') prefix += "/";
    std::error_code ec;
    for (fs::recursive_directory_iterator it(path("")), end; it != end; it.increment(ec)) {
      if (ec) break;
      if (!it->is_regular_file()) continue;
      std::string rel = "/" + fs::relative(it->path(), root_, ec).generic_string();
      if (rel.compare(0, prefix.size(), prefix) == 0) out.push_back(rel);
    }
    return out;
  }

  size_t usedBytes() {
    size_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(path("")), end; it != end; it.increment(ec)) {
      if (ec) break;
      if (it->is_regular_file()) total += static_cast<size_t>(fs::file_size(it->path(), ec));
    }
    return total;
  }

  size_t totalBytes() { return 1024 * 1024; }

 private:
  std::string path(const std::string& p) const {
    if (p.empty()) return root_;
    if (p[0] == '/') return root_ + p;
    return root_ + "/" + p;
  }
  std::string root_;
};

std::string statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 423: return "Locked";
    case 429: return "Too Many Requests";
    default: return "Internal Server Error";
  }
}

std::string lower(std::string s) {
  for (size_t k = 0; k < s.size(); ++k)
    if (s[k] >= 'A' && s[k] <= 'Z') s[k] = static_cast<char>(s[k] - 'A' + 'a');
  return s;
}

bool readRequest(SOCKET client, std::string& method, std::string& path, std::string& auth,
                 std::string& body) {
  std::string buffer;
  char chunk[1024];
  size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos) {
    int n = recv(client, chunk, sizeof(chunk), 0);
    if (n <= 0) return false;
    buffer.append(chunk, static_cast<size_t>(n));
    headerEnd = buffer.find("\r\n\r\n");
    if (buffer.size() > 64 * 1024) return false;
  }

  std::string head = buffer.substr(0, headerEnd);
  size_t lineEnd = head.find("\r\n");
  std::string requestLine = head.substr(0, lineEnd == std::string::npos ? head.size() : lineEnd);
  size_t sp1 = requestLine.find(' ');
  size_t sp2 = requestLine.rfind(' ');
  if (sp1 == std::string::npos || sp2 == std::string::npos || sp2 <= sp1) return false;
  method = requestLine.substr(0, sp1);
  path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);

  size_t contentLength = 0;
  size_t pos = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
  while (pos < head.size()) {
    size_t end = head.find("\r\n", pos);
    std::string line = head.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = lower(line.substr(0, colon));
      std::string value = line.substr(colon + 1);
      while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
      while (!value.empty() && (value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t'))
        value.erase(value.size() - 1);
      if (name == "content-length") contentLength = static_cast<size_t>(std::strtoul(value.c_str(), 0, 10));
      if (name == "authorization") auth = value;
    }
    if (end == std::string::npos) break;
    pos = end + 2;
  }

  body = buffer.substr(headerEnd + 4);
  while (body.size() < contentLength) {
    int n = recv(client, chunk, sizeof(chunk), 0);
    if (n <= 0) break;
    body.append(chunk, static_cast<size_t>(n));
  }
  if (body.size() > contentLength) body.resize(contentLength);
  return true;
}

void sendAll(SOCKET client, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
    if (n <= 0) return;
    sent += static_cast<size_t>(n);
  }
}

}  // namespace

int main(int argc, char** argv) {
  int port = argc > 1 ? std::atoi(argv[1]) : 8080;
  std::string dataDir = argc > 2 ? argv[2] : ".sim-data";

#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("WSAStartup failed\n");
    return 1;
  }
#endif

  FileStorage storage(dataDir);
  storage.begin();
  Classroom classroom(&storage, "246810");
  classroom.setSeed(static_cast<unsigned long>(
      std::chrono::system_clock::now().time_since_epoch().count()));
  bool loaded = classroom.load();

  auto started = std::chrono::steady_clock::now();
  DeviceInfo info;
  info.name = "SNTCH-SIM";
  info.ssid = "SNTCH-CLASS";
  info.ip = "127.0.0.1";
  info.firmware = "0.1.0-host-sim";
  Api api(&classroom, info);

  SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == INVALID_SOCKET) {
    std::printf("socket() failed\n");
    return 1;
  }
  int reuse = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<unsigned short>(port));
  if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::printf("bind(%d) failed\n", port);
    return 1;
  }
  listen(server, 8);
  std::printf("SNTCH host simulator on http://127.0.0.1:%d  data=%s  loaded=%s\n", port,
              dataDir.c_str(), loaded ? "yes" : "no");
  std::fflush(stdout);

  auto now = [&started]() -> Ms {
    return static_cast<Ms>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
            .count());
  };

  for (;;) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(server, &readSet);
    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
#ifdef _WIN32
    int ready = select(0, &readSet, 0, 0, &timeout);
#else
    int ready = select(server + 1, &readSet, 0, 0, &timeout);
#endif
    classroom.tick(now());
    if (ready <= 0) continue;

    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    SOCKET client = accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (client == INVALID_SOCKET) continue;
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuffer, sizeof(ipBuffer));

    std::string method, path, auth, body;
    if (readRequest(client, method, path, auth, body)) {
      Request request;
      request.method = method;
      request.path = path;
      request.auth = auth;
      request.body = body;
      request.ip = ipBuffer;
      request.nowMs = now();

      std::error_code ec;
      info.storageUsed = storage.usedBytes();
      info.storageTotal = storage.totalBytes();
      info.freeHeap = 200000;
      api.setDeviceInfo(info);

      Response response = api.handle(request);
      std::string head = "HTTP/1.1 " + std::to_string(response.status) + " " +
                         statusText(response.status) + "\r\nContent-Type: " + response.contentType +
                         "\r\nContent-Length: " + std::to_string(response.body.size()) +
                         "\r\nConnection: close\r\n\r\n";
      sendAll(client, head + response.body);
    }
    closesocket(client);
  }
  return 0;
}
