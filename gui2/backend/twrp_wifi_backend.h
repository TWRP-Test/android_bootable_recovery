#ifndef GUI2_BACKEND_TWRP_WIFI_BACKEND_H
#define GUI2_BACKEND_TWRP_WIFI_BACKEND_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "wifi_backend.h"

namespace gui2_backend {

class twrp_wifi_backend final : public wifi_backend {
 public:
  twrp_wifi_backend() = default;
  ~twrp_wifi_backend() override;

  twrp_wifi_backend(const twrp_wifi_backend&) = delete;
  twrp_wifi_backend& operator=(const twrp_wifi_backend&) = delete;

  bool available() override;
  bool service_running() override;
  bool set_service(bool running) override;
  size_t log_count() override;
  std::string log_line(size_t index) override;
  bool start_scan() override;
  std::vector<wifi_network> networks() override;
  bool start_connect(const std::string& ssid, wifi_security security,
                     const std::string& password) override;
  std::string connected_ssid() override;
  bool start_status() override;
  bool start_test() override;
  wifi_state state() override;
  void acknowledge() override;

 private:
  bool begin(wifi_state running_state);
  void join_finished_thread();
  void finish(bool ok);

  void run_scan();
  void run_connect(std::string ssid, wifi_security security, std::string password);
  void run_status();
  void run_test();

  std::mutex mutex_;
  wifi_state state_ = wifi_state::IDLE;
  std::vector<wifi_network> networks_;
  std::atomic<bool> running_{ false };
  std::thread worker_;

  // The status bar's view of the link, refreshed off the UI thread.
  void refresh_connected();
  void note(const char* format, ...) __attribute__((format(printf, 2, 3)));
  std::vector<std::string> log_;
  // Lines trimmed off the front of log_, so indices stay stable.
  size_t log_dropped_ = 0;
  std::string connected_;
  std::atomic<bool> refreshing_{ false };
  std::thread refresher_;
  long long refreshed_at_ms_ = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_WIFI_BACKEND_H
