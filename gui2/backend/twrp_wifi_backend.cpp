#include "twrp_wifi_backend.h"

#include <unistd.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include <android-base/properties.h>

#include "data.hpp"
#include "twcommon.h"

namespace gui2_backend {

namespace {

// The same interface and control socket the legacy WLAN page uses; see the
// wlan* actions in gui/action.cpp.
constexpr const char* kInterface = "wlan0";
constexpr const char* kControlDir = "/tmp/recovery/sockets";
constexpr const char* kService = "wpa_supplicant";
constexpr size_t kLogLimit = 400;

std::string run(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) return std::string();

  std::string output;
  char buffer[512];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
  pclose(pipe);
  return output;
}

std::string wpa(const std::string& arguments) {
  return run(std::string("wpa_cli -i") + kInterface + " -p" + kControlDir + " " + arguments);
}

std::string trimmed(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
    value.pop_back();
  size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
  return value.substr(start);
}

// wpa_cli prints anything outside printable ASCII as \xNN.
std::string unescaped(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size();) {
    if (i + 3 < value.size() && value[i] == '\\' && value[i + 1] == 'x') {
      const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
      };
      const int high = digit(value[i + 2]);
      const int low = digit(value[i + 3]);
      if (high >= 0 && low >= 0) {
        out.push_back(static_cast<char>((high << 4) | low));
        i += 4;
        continue;
      }
    }
    out.push_back(value[i]);
    ++i;
  }
  return out;
}

wifi_security security_from_flags(const std::string& flags) {
  if (flags.find("SAE") != std::string::npos) return wifi_security::WPA3;
  if (flags.find("WPA2") != std::string::npos) return wifi_security::WPA2;
  if (flags.find("WPA") != std::string::npos) return wifi_security::WPA;
  return wifi_security::OPEN;
}

const char* key_management_for(wifi_security security) {
  switch (security) {
    case wifi_security::WPA3:
      return "SAE";
    case wifi_security::WPA2:
    case wifi_security::WPA:
      return "WPA-PSK";
    case wifi_security::OPEN:
      break;
  }
  return "NONE";
}

// Single quotes stop the shell from touching anything but a quote itself. The
// value still carries the double quotes wpa_cli wants around an SSID or a psk.
std::string quoted(const std::string& value) {
  std::string out = "'";
  for (const char c : value) {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

std::string first_line_of(const std::string& command) {
  return trimmed(run(command));
}

}  // namespace

twrp_wifi_backend::~twrp_wifi_backend() {
  if (worker_.joinable()) worker_.join();
  if (refresher_.joinable()) refresher_.join();
}

void twrp_wifi_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

void twrp_wifi_backend::note(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  LOGINFO("wlan: %s", buffer);

  // One entry per line; a status dump arrives as a single block.
  std::lock_guard<std::mutex> lock(mutex_);
  std::string text = buffer;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    const std::string line = text.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
    if (!line.empty()) log_.push_back(line);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (log_.size() > kLogLimit) {
    const size_t excess = log_.size() - kLogLimit;
    log_.erase(log_.begin(), log_.begin() + excess);
    log_dropped_ += excess;
  }
}

size_t twrp_wifi_backend::log_count() {
  std::lock_guard<std::mutex> lock(mutex_);
  return log_dropped_ + log_.size();
}

std::string twrp_wifi_backend::log_line(size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < log_dropped_ || index - log_dropped_ >= log_.size()) return std::string();
  return log_[index - log_dropped_];
}

bool twrp_wifi_backend::service_running() {
  return android::base::GetProperty(std::string("init.svc.") + kService, "") == "running";
}

bool twrp_wifi_backend::set_service(bool running) {
  if (!available() || running_.load()) return false;
  note(running ? "Starting WLAN service...\n" : "Stopping WLAN service...\n");
  if (!android::base::SetProperty(running ? "ctl.start" : "ctl.stop", kService)) {
    note("Could not ask init to %s %s\n", running ? "start" : "stop", kService);
    return false;
  }
  if (!running) {
    std::lock_guard<std::mutex> lock(mutex_);
    networks_.clear();
    connected_.clear();
  }
  return true;
}

bool twrp_wifi_backend::available() {
  return DataManager::GetIntValue("tw_disable_network") == 0;
}

bool twrp_wifi_backend::begin(wifi_state running_state) {
  if (running_.load() || !available()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = running_state;
  }
  running_.store(true);
  return true;
}

void twrp_wifi_backend::finish(bool ok) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ok ? wifi_state::DONE : wifi_state::FAILED;
  }
  running_.store(false);
}

bool twrp_wifi_backend::start_scan() {
  if (!begin(wifi_state::SCANNING)) return false;
  worker_ = std::thread(&twrp_wifi_backend::run_scan, this);
  return true;
}

// The driver answers a scan request asynchronously, so scan_results is polled
// until it has more than its header line or the wait runs out.
void twrp_wifi_backend::run_scan() {
  note("Scanning for networks...\n");

  const std::string reply = wpa("scan");
  if (reply.find("OK") == std::string::npos && reply.find("FAIL-BUSY") == std::string::npos) {
    note("Could not start a scan: %s", reply.c_str());
    finish(false);
    return;
  }
  if (reply.find("FAIL-BUSY") != std::string::npos)
    note("A scan was already running; waiting for its results\n");

  std::vector<std::string> lines;
  constexpr int kTimeoutMs = 10000;
  constexpr int kIntervalMs = 500;
  for (int elapsed = 0; elapsed <= kTimeoutMs; elapsed += kIntervalMs) {
    lines.clear();
    std::istringstream results(wpa("scan_results"));
    std::string line;
    while (std::getline(results, line)) {
      line = trimmed(line);
      if (!line.empty()) lines.push_back(line);
    }
    if (lines.size() > 1) break;
    usleep(kIntervalMs * 1000);
  }

  std::vector<wifi_network> found;
  // The first line is the column header: bssid / frequency / signal / flags / ssid.
  for (size_t i = 1; i < lines.size(); ++i) {
    std::istringstream columns(lines[i]);
    std::string bssid;
    std::string frequency;
    std::string signal;
    std::string flags;
    if (!(columns >> bssid >> frequency >> signal >> flags)) continue;

    std::string ssid;
    std::getline(columns, ssid);
    ssid = unescaped(trimmed(ssid));
    if (ssid.empty()) continue;  // a hidden network has nothing to show

    wifi_network network;
    network.ssid = ssid;
    network.bssid = bssid;
    network.signal_dbm = atoi(signal.c_str());
    network.security = security_from_flags(flags);
    found.push_back(std::move(network));
  }

  note("Found %zu network(s)\n", found.size());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    networks_ = std::move(found);
  }
  finish(true);
}

std::vector<wifi_network> twrp_wifi_backend::networks() {
  std::lock_guard<std::mutex> lock(mutex_);
  return networks_;
}

bool twrp_wifi_backend::start_connect(const std::string& ssid, wifi_security security,
                                      const std::string& password) {
  if (ssid.empty()) return false;
  if (security != wifi_security::OPEN && password.empty()) return false;
  if (!begin(wifi_state::CONNECTING)) return false;
  worker_ = std::thread(&twrp_wifi_backend::run_connect, this, ssid, security, password);
  return true;
}

void twrp_wifi_backend::run_connect(std::string ssid, wifi_security security,
                                    std::string password) {
  note("Connecting to %s...\n", ssid.c_str());

  // One network entry, rebuilt every time, exactly as the legacy page does it.
  wpa("remove_network 0");
  wpa("add_network");
  wpa("set_network 0 ssid " + quoted("\"" + ssid + "\""));
  wpa(std::string("set_network 0 key_mgmt ") + key_management_for(security));
  if (security != wifi_security::OPEN)
    wpa("set_network 0 psk " + quoted("\"" + password + "\""));
  wpa("enable_network 0");

  note("Authenticating...\n");
  bool connected = false;
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (wpa("status").find("wpa_state=COMPLETED") != std::string::npos) {
      connected = true;
      break;
    }
    sleep(1);
  }

  if (!connected) {
    note("Failed to connect: timed out or the password was refused\n");
    finish(false);
    return;
  }

  note("Link established, requesting an address with dhcpcd...\n");
  run(std::string("dhcpcd ") + kInterface);
  sleep(5);

  const std::string address = first_line_of(std::string("ifconfig ") + kInterface +
                                            " | grep 'inet ' | awk -F'[: ]+' '{print $4}'");
  const std::string gateway = first_line_of(std::string("netstat -rn | grep ") + kInterface +
                                            " | grep UG | awk '{print $2}'");
  note("Connected\n");
  note("IP address: %s\n", address.empty() ? "(unknown)" : address.c_str());
  note("Gateway: %s\n", gateway.empty() ? "(unknown)" : gateway.c_str());
  finish(true);
}

void twrp_wifi_backend::refresh_connected() {
  std::string ssid;
  const std::string status = wpa("status");
  if (status.find("wpa_state=COMPLETED") != std::string::npos) {
    std::istringstream lines(status);
    std::string line;
    while (std::getline(lines, line)) {
      if (line.rfind("ssid=", 0) == 0) {
        ssid = unescaped(trimmed(line.substr(5)));
        break;
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = ssid;
  }
  refreshing_.store(false);
}

// wpa_cli can take seconds when the supplicant is not answering, which would
// freeze the UI loop that calls this. Hand back what is known and ask again in
// the background.
std::string twrp_wifi_backend::connected_ssid() {
  if (!available()) return std::string();

  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  if (!refreshing_.load() && now - refreshed_at_ms_ >= 2000) {
    if (refresher_.joinable()) refresher_.join();
    refreshed_at_ms_ = now;
    refreshing_.store(true);
    refresher_ = std::thread(&twrp_wifi_backend::refresh_connected, this);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

bool twrp_wifi_backend::start_status() {
  if (!begin(wifi_state::SCANNING)) return false;
  worker_ = std::thread(&twrp_wifi_backend::run_status, this);
  return true;
}

void twrp_wifi_backend::run_status() {
  note("=== WLAN status ===\n");
  note("Interface: %s\n", kInterface);

  const std::string status = wpa("status");
  if (status.empty()) {
    note("No reply from the supplicant\n");
    finish(false);
    return;
  }
  note("%s", status.c_str());

  const std::string mac =
      first_line_of(std::string("ifconfig ") + kInterface + " | awk '/HWaddr/ {print $5}'");
  if (!mac.empty()) note("MAC: %s\n", mac.c_str());
  finish(true);
}

bool twrp_wifi_backend::start_test() {
  if (!begin(wifi_state::SCANNING)) return false;
  worker_ = std::thread(&twrp_wifi_backend::run_test, this);
  return true;
}

void twrp_wifi_backend::run_test() {
  note("Testing the connection...\n");

  const std::string gateway = first_line_of("netstat -rn | grep '^0.0.0.0' | awk '{print $2}'");
  bool gateway_ok = false;
  if (gateway.empty()) {
    note("Could not determine the gateway\n");
  } else {
    note("Pinging the gateway (%s)...\n", gateway.c_str());
    const std::string reply = run("busybox ping -I " + std::string(kInterface) + " -c 1 " +
                                  quoted(gateway));
    gateway_ok = reply.find("0% packet loss") != std::string::npos;
    note("Gateway ping: %s\n", gateway_ok ? "ok" : "failed");
  }

  const std::string outside =
      run("busybox ping -I " + std::string(kInterface) + " -c 4 8.8.8.8");
  const bool reachable = outside.find("0% packet loss") != std::string::npos;
  note("Internet ping: %s\n", reachable ? "ok" : "failed");

  finish(gateway_ok || reachable);
}

wifi_state twrp_wifi_backend::state() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void twrp_wifi_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != wifi_state::SCANNING && state_ != wifi_state::CONNECTING)
    state_ = wifi_state::IDLE;
}

}  // namespace gui2_backend
