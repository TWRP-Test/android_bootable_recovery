#ifndef GUI2_BACKEND_TWRP_INSTALL_BACKEND_H
#define GUI2_BACKEND_TWRP_INSTALL_BACKEND_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "install_backend.h"

namespace gui2_backend {

class twrp_install_backend final : public install_backend {
 public:
  ~twrp_install_backend() override;

  bool start_zip(const std::vector<std::string>& paths) override;
  std::vector<image_target> image_targets() override;
  std::string image_target_for(const std::string& filename) override;
  bool start_image(const std::string& path, const std::string& mount_point,
                   bool both_slots) override;
  install_status status() override;
  void acknowledge() override;

 private:
  void run_zip(std::vector<std::string> zip_queue);
  void run_image();
  void join_finished_thread();

  std::thread worker_;
  std::atomic<bool> running_{ false };
  std::mutex mutex_;
  install_state state_ = install_state::IDLE;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_INSTALL_BACKEND_H
