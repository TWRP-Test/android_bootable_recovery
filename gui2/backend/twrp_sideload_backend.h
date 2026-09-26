#ifndef GUI2_BACKEND_TWRP_SIDELOAD_BACKEND_H
#define GUI2_BACKEND_TWRP_SIDELOAD_BACKEND_H

#include <atomic>
#include <mutex>
#include <thread>

#include "sideload_backend.h"

namespace gui2_backend {

class twrp_sideload_backend final : public sideload_backend {
 public:
  ~twrp_sideload_backend() override;

  bool start(bool wipe_dalvik, bool wipe_cache) override;
  void cancel() override;
  sideload_status status() override;
  void acknowledge() override;

 private:
  void run(bool wipe_dalvik, bool wipe_cache);
  void stop_child();
  void join_threads();

  std::thread worker_;
  std::thread canceller_;
  std::atomic<bool> running_{ false };
  std::atomic<bool> cancelled_{ false };
  std::mutex mutex_;
  sideload_state state_ = sideload_state::IDLE;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_SIDELOAD_BACKEND_H
