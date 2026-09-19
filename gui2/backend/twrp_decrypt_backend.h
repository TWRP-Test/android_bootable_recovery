#ifndef GUI2_BACKEND_TWRP_DECRYPT_BACKEND_H
#define GUI2_BACKEND_TWRP_DECRYPT_BACKEND_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "decrypt_backend.h"

namespace gui2_backend {

class twrp_decrypt_backend final : public decrypt_backend {
 public:
  twrp_decrypt_backend() = default;
  ~twrp_decrypt_backend() override;

  twrp_decrypt_backend(const twrp_decrypt_backend&) = delete;
  twrp_decrypt_backend& operator=(const twrp_decrypt_backend&) = delete;

  bool is_encrypted() override;
  lock_kind kind() override;
  bool start(const std::string& password) override;
  bool start_refresh() override;
  decrypt_state state() override;
  void acknowledge() override;

 private:
  void run(std::string password);
  void run_refresh();
  void join_finished_thread();

  std::mutex mutex_;
  decrypt_state state_ = decrypt_state::IDLE;
  std::atomic<bool> running_{ false };
  std::thread worker_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_DECRYPT_BACKEND_H
