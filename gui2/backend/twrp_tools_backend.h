#ifndef GUI2_BACKEND_TWRP_TOOLS_BACKEND_H
#define GUI2_BACKEND_TWRP_TOOLS_BACKEND_H

#include <atomic>
#include <mutex>
#include <thread>

#include "tools_backend.h"

namespace gui2_backend {

class twrp_tools_backend final : public tools_backend {
 public:
  ~twrp_tools_backend() override;

  bool details(const std::string& mount_point, partition_details* out) override;
  tool_availability availability() override;
  std::string twrp_folder() override;
  std::string storage_path() override;
  bool path_exists(const std::string& path) override;
  bool users_locked() override;
  bool start(tool_job job, const std::string& target, const std::string& value) override;
  tool_state status() override;
  void acknowledge() override;

 private:
  void run(tool_job job, std::string target, std::string value);
  void join_finished_thread();

  std::thread worker_;
  std::atomic<bool> running_{ false };
  std::mutex mutex_;
  tool_state state_ = tool_state::IDLE;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_TOOLS_BACKEND_H
