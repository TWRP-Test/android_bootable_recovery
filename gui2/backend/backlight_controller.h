#ifndef GUI2_BACKEND_BACKLIGHT_CONTROLLER_H
#define GUI2_BACKEND_BACKLIGHT_CONTROLLER_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace gui2_backend {

// Serializes slider writes with temporary dim/off values. Only a successful
// write can be reused; transitions invalidate it because blanking resets panels.
class backlight_controller {
 public:
  explicit backlight_controller(std::function<bool(int)> write);
  ~backlight_controller();
  backlight_controller(const backlight_controller&) = delete;
  backlight_controller& operator=(const backlight_controller&) = delete;

  bool set_user(int value);
  bool override_output(int value);
  bool restore_user(int value);
  bool consume_error();
  void stop();

 private:
  bool transition(int value, bool suspend);
  bool write_locked(int value, std::unique_lock<std::mutex>& lock);
  void run();

  std::function<bool(int)> write_;
  std::mutex mutex_;
  std::condition_variable wake_;
  int pending_ = -1;
  int applied_ = -1;
  bool suspended_ = false;
  bool busy_ = false;
  bool stopping_ = false;
  bool error_ = false;
  std::thread worker_;
};

}  // namespace gui2_backend
#endif
