#ifndef GUI2_BACKEND_STATUS_BACKEND_H
#define GUI2_BACKEND_STATUS_BACKEND_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace gui2_backend {

class settings_store;

struct status_snapshot {
  std::string time_text;
  int battery_percentage = 0;
  bool charging = false;
  bool battery_valid = false;
};

class status_backend final {
 public:
  explicit status_backend(settings_store* settings);
  ~status_backend();

  status_backend(const status_backend&) = delete;
  status_backend& operator=(const status_backend&) = delete;

  void start();
  void stop();
  status_snapshot snapshot() const;

 private:
  void run();
  void update_once();

  settings_store* settings_;
  mutable std::mutex state_mutex_;
  status_snapshot state_;
  std::mutex wait_mutex_;
  std::condition_variable wait_condition_;
  bool stop_requested_ = false;
  std::thread worker_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_STATUS_BACKEND_H
