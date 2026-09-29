#ifndef GUI2_BACKEND_TWRP_WIPE_BACKEND_H
#define GUI2_BACKEND_TWRP_WIPE_BACKEND_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "settings_store.h"
#include "wipe_backend.h"

namespace gui2_backend {

class twrp_wipe_backend final : public wipe_backend {
 public:
  explicit twrp_wipe_backend(settings_store* settings) : settings_(settings) {}
  ~twrp_wipe_backend() override;

  twrp_wipe_backend(const twrp_wipe_backend&) = delete;
  twrp_wipe_backend& operator=(const twrp_wipe_backend&) = delete;

  std::vector<wipe_target> targets() override;
  bool has_data_media() override;

  bool start_factory_reset() override;
  bool start_format_data() override;
  bool start_cache_dalvik() override;
  bool start_wipe(const std::vector<std::string>& mount_points) override;

  wipe_status status() override;

 private:
  // Arguments of GUIAction::wipe, run one after the other.
  bool start(std::vector<std::string> args);
  void run(std::vector<std::string> args);
  void join_finished_thread();

  settings_store* settings_;
  std::mutex mutex_;
  wipe_status status_;
  std::atomic<bool> running_{ false };
  std::thread worker_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_WIPE_BACKEND_H
