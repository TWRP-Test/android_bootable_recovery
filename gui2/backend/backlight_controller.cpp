#include "backlight_controller.h"

#include <utility>

namespace gui2_backend {

backlight_controller::backlight_controller(std::function<bool(int)> write)
    : write_(std::move(write)), worker_(&backlight_controller::run, this) {}

backlight_controller::~backlight_controller() { stop(); }

bool backlight_controller::set_user(int value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || value < 0) return false;
  if (!suspended_) {
    pending_ = value;
    wake_.notify_one();
  }
  return true;
}

bool backlight_controller::write_locked(int value, std::unique_lock<std::mutex>& lock) {
  busy_ = true;
  lock.unlock();
  const bool success = write_(value);
  lock.lock();
  busy_ = false;
  applied_ = success ? value : -1;
  error_ = error_ || !success;
  wake_.notify_all();
  return success;
}

bool backlight_controller::transition(int value, bool suspend) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (stopping_ || value < 0) return false;
  suspended_ = true;
  pending_ = -1;
  wake_.wait(lock, [this] { return !busy_; });
  // Even a repeated value must be rewritten after a framebuffer transition.
  const bool success = write_locked(value, lock);
  suspended_ = suspend;
  return success;
}

bool backlight_controller::override_output(int value) { return transition(value, true); }

bool backlight_controller::restore_user(int value) { return transition(value, false); }

bool backlight_controller::consume_error() {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool error = error_;
  error_ = false;
  return error;
}

void backlight_controller::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    wake_.notify_all();
  }
  if (worker_.joinable()) worker_.join();
}

void backlight_controller::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    wake_.wait(lock, [this] { return pending_ >= 0 || stopping_; });
    if (pending_ < 0 && stopping_) return;
    const int value = pending_;
    pending_ = -1;
    if (value != applied_) write_locked(value, lock);
  }
}

}  // namespace gui2_backend
