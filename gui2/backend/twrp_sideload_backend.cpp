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
#include "twrp_operation.h"

// The child's pid, shared with the legacy action code.
extern pid_t sideload_child_pid;

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
  // The sideload page's two checkboxes.
  DataManager::SetValue("tw_wipe_dalvik", wipe_dalvik ? 1 : 0);
  DataManager::SetValue("tw_wipe_cache", wipe_cache ? 1 : 0);
  DataManager::SetValue("tw_has_cancel", 1);
  running_.store(true);
  worker_ = std::thread(&twrp_sideload_backend::run, this);
  return true;
}

// GUIAction::adbsideload
void twrp_sideload_backend::run() {
  operation_start("Sideload");
  gui_msg("start_sideload=Starting ADB sideload feature...");
  bool mtp_was_enabled = TWFunc::Toggle_MTP(false);

  // wait for the adb connection
  Device::BuiltinAction reboot_action = Device::REBOOT_BOOTLOADER;
  int ret = twrp_sideload("/", &reboot_action);
  sideload_child_pid = GetMiniAdbdPid();
  DataManager::SetValue("tw_has_cancel", 0);  // Remove cancel button from gui now that the zip install is going to start

  if (ret != 0) {
    if (ret == -2) gui_msg("need_new_adb=You need adb 1.0.32 or newer to sideload to this device.");
    ret = 1;  // failure
  } else {
    int wipe_cache = 0;
    int wipe_dalvik = 0;
    DataManager::GetValue("tw_wipe_dalvik", wipe_dalvik);
    if (wipe_cache || DataManager::GetIntValue("tw_wipe_cache")) PartitionManager.Wipe_By_Path("/cache");
    if (wipe_dalvik) PartitionManager.Wipe_Dalvik_Cache();
  }
  TWFunc::Toggle_MTP(mtp_was_enabled);
  operation_end(ret);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_.load())
      state_ = sideload_state::CANCELLED;
    else
      state_ = ret == 0 ? sideload_state::DONE : sideload_state::FAILED;
  }
  running_.store(false);
}

// GUIAction::adbsideloadcancel
void twrp_sideload_backend::stop_child() {
  struct stat st;
  DataManager::SetValue("tw_has_cancel", 0);  // Remove cancel button from gui
  gui_msg("cancel_sideload=Cancelling ADB sideload...");
  LOGINFO("Signaling child sideload process to exit.\n");
  // Calling stat() on this magic filename signals the minadbd
  // subprocess to shut down.
  stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
  sideload_child_pid = GetMiniAdbdPid();
  if (!sideload_child_pid) {
    LOGERR("Unable to get child ID\n");
    return;
  }
  ::sleep(1);
  LOGINFO("Killing child sideload process.\n");
  kill(sideload_child_pid, SIGTERM);
  int status;
  LOGINFO("Waiting for child sideload process to exit.\n");
  waitpid(sideload_child_pid, &status, 0);
  sideload_child_pid = 0;
  DataManager::SetValue("tw_page_done", "1");  // For OpenRecoveryScript support
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
