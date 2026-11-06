#ifndef GUI2_APP_STORAGE_TASK_H
#define GUI2_APP_STORAGE_TASK_H

#include <atomic>
#include <thread>
#include <utility>

namespace gui2_app {

// Owned by the GUI lifecycle. The worker never owns LVGL objects.
class storage_task {
 public:
  ~storage_task() { join(); }
  storage_task() = default;
  storage_task(const storage_task&) = delete;
  storage_task& operator=(const storage_task&) = delete;

  template <typename Function>
  void start(Function&& function) {
    join();
    finished_.store(false);
    worker_ = std::thread([this, task = std::forward<Function>(function)]() mutable {
      result_ = task();
      finished_.store(true);
    });
  }

  bool take_result(bool* result) {
    if (!worker_.joinable() || !finished_.load()) return false;
    join();
    *result = result_;
    return true;
  }

  void join() {
    if (worker_.joinable()) worker_.join();
  }

 private:
  std::thread worker_;
  std::atomic<bool> finished_{false};
  bool result_ = false;
};

}  // namespace gui2_app

#endif
