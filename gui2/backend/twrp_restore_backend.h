#ifndef GUI2_BACKEND_TWRP_RESTORE_BACKEND_H
#define GUI2_BACKEND_TWRP_RESTORE_BACKEND_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "restore_backend.h"

namespace gui2_backend {

class twrp_restore_backend final : public restore_backend {
 public:
  twrp_restore_backend() = default;
  ~twrp_restore_backend() override;

  twrp_restore_backend(const twrp_restore_backend&) = delete;
  twrp_restore_backend& operator=(const twrp_restore_backend&) = delete;

  std::vector<restore_backup> backups() override;
  bool open(const std::string& path) override;
  std::vector<restore_target> targets() override;
  bool encrypted() override;
  std::string date() override;
  bool unlock(const std::string& password) override;
  bool start(const std::vector<std::string>& mount_points, bool check_digest) override;
  restore_status status() override;
  void acknowledge() override;

 private:
  void run(std::string path);
  void join_finished_thread();

  std::mutex mutex_;
  restore_state state_ = restore_state::IDLE;
  std::atomic<bool> running_{ false };
  std::thread worker_;
  // Everything below open() describes this folder, the way the legacy pages
  // describe whatever tw_restore points at.
  std::string opened_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_RESTORE_BACKEND_H
