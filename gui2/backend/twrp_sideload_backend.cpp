#include "twrp_sideload_backend.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "data.hpp"
#include "fuse_sideload.h"
#include "gui/gui.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "twinstall/adb_install.h"
#include "twrp-functions.hpp"

namespace gui2_backend {

twrp_sideload_backend::~twrp_sideload_backend() {
  if (running_.load()) stop_child();
  if (worker_.joinable()) worker_.join();
  if (canceller_.joinable()) canceller_.join();
}

void twrp_sideload_backend::join_threads() {
  if (running_.load()) return;
  if (worker_.joinable()) worker_.join();
  if (canceller_.joinable()) canceller_.join();
}

bool twrp_sideload_backend::start(bool wipe_dalvik, bool wipe_cache) {
  if (running_.load()) return false;
  join_threads();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = sideload_state::RUNNING;
  }
  cancelled_.store(false);
  DataManager::SetValue("ui_progress", 0);
  DataManager::SetValue("ui_portion_size", 0);
  DataManager::SetValue("ui_portion_start", 0);
  running_.store(true);
  worker_ = std::thread(&twrp_sideload_backend::run, this, wipe_dalvik, wipe_cache);
  return true;
}

// GUIAction::adbsideload, with the two wipe options passed in instead of read
// from the page's variables.
void twrp_sideload_backend::run(bool wipe_dalvik, bool wipe_cache) {
  gui_msg("start_sideload=Starting ADB sideload feature...");
  const bool mtp_was_enabled = TWFunc::Toggle_MTP(false);

  Device::BuiltinAction reboot_action = Device::REBOOT_BOOTLOADER;
  int ret = twrp_sideload("/", &reboot_action);
  if (ret != 0) {
    if (ret == -2) gui_msg("need_new_adb=You need adb 1.0.32 or newer to sideload to this device.");
  } else {
    if (wipe_cache) PartitionManager.Wipe_By_Path("/cache");
    if (wipe_dalvik) PartitionManager.Wipe_Dalvik_Cache();
  }
  TWFunc::Toggle_MTP(mtp_was_enabled);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_.load())
      state_ = sideload_state::CANCELLED;
    else
      state_ = ret == 0 ? sideload_state::DONE : sideload_state::FAILED;
  }
  running_.store(false);
}

// GUIAction::adbsideloadcancel.
void twrp_sideload_backend::stop_child() {
  struct stat st;
  gui_msg("cancel_sideload=Cancelling ADB sideload...");
  LOGINFO("Signaling child sideload process to exit.\n");
  // Calling stat() on this magic filename signals the minadbd subprocess to
  // shut down.
  stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
  pid_t child = GetMiniAdbdPid();
  if (!child) {
    LOGERR("Unable to get child ID\n");
    return;
  }
  ::sleep(1);
  LOGINFO("Killing child sideload process.\n");
  kill(child, SIGTERM);
  int status;
  LOGINFO("Waiting for child sideload process to exit.\n");
  waitpid(child, &status, 0);
}

void twrp_sideload_backend::cancel() {
  if (!running_.load() || cancelled_.exchange(true)) return;
  // It sleeps and waits for the child; keep that off the UI thread.
  canceller_ = std::thread(&twrp_sideload_backend::stop_child, this);
}

sideload_status twrp_sideload_backend::status() {
  sideload_status result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.state = state_;
  if (state_ == sideload_state::RUNNING) {
    const int progress = std::atoi(DataManager::GetStrValue("ui_progress").c_str());
    if (progress > 0) result.progress = std::min(progress, 100);
  }
  return result;
}

void twrp_sideload_backend::acknowledge() {
  join_threads();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != sideload_state::RUNNING) state_ = sideload_state::IDLE;
}

}  // namespace gui2_backend
