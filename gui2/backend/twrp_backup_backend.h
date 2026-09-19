#ifndef GUI2_BACKEND_TWRP_BACKUP_BACKEND_H
#define GUI2_BACKEND_TWRP_BACKUP_BACKEND_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "backup_backend.h"

namespace gui2_backend {

class twrp_backup_backend final : public backup_backend {
 public:
  twrp_backup_backend() = default;
  ~twrp_backup_backend() override;

  twrp_backup_backend(const twrp_backup_backend&) = delete;
  twrp_backup_backend& operator=(const twrp_backup_backend&) = delete;

  std::vector<backup_target> targets() override;
  bool start(const std::vector<std::string>& mount_points, const std::string& name, bool compress,
             bool skip_digest, bool encrypt, const std::string& password) override;
  void cancel() override;
  backup_status status() override;
  void acknowledge() override;

 private:
  void run();
  void join_finished_thread();

  std::mutex mutex_;
  backup_state state_ = backup_state::IDLE;
  std::atomic<bool> running_{ false };
  std::thread worker_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_BACKUP_BACKEND_H
