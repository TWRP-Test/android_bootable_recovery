#include "twrp_restore_backend.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp_functions.hpp"
#include "twrp_file_list.h"
#include "twrp_operation.h"
#include "variables.h"

namespace gui2_backend {

namespace {

// One backup folder is a handful of archives, so summing them is cheap enough
// to do while the list is drawn.
uint64_t folder_size(const std::string& path) {
  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) return 0;

  uint64_t total = 0;
  while (const dirent* entry = readdir(directory)) {
    if (entry->d_name[0] == '.') continue;
    struct stat info;
    if (lstat((path + "/" + entry->d_name).c_str(), &info) == 0 && S_ISREG(info.st_mode))
      total += static_cast<uint64_t>(info.st_size);
  }
  closedir(directory);
  return total;
}

std::string human_size(uint64_t bytes) {
  static const char* const kUnits[] = { "B", "KB", "MB", "GB", "TB" };
  constexpr size_t kUnitCount = 5;
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < kUnitCount) {
    value /= 1024.0;
    ++unit;
  }
  char text[32];
  std::snprintf(text, sizeof(text), unit == 0 ? "%.0f %s" : "%.1f %s", value, kUnits[unit]);
  return text;
}

std::string human_time(time_t when) {
  struct tm parts;
  if (localtime_r(&when, &parts) == nullptr) return std::string();
  char text[32];
  if (std::strftime(text, sizeof(text), "%d %b %Y %H:%M", &parts) == 0) return std::string();
  return text;
}

}  // namespace

twrp_restore_backend::~twrp_restore_backend() {
  if (worker_.joinable()) worker_.join();
}

void twrp_restore_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

// List local backup folders in tw_gui_sort_order.
std::vector<restore_backup> twrp_restore_backend::backups() {
  std::string folder;
  DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, folder);
  if (folder.empty()) return {};

  const file_listing listing = twrp_file_list(folder, {}, true);
  std::vector<restore_backup> result;
  result.reserve(listing.folders.size());
  for (const file_entry& entry : listing.folders) {
    const std::string path = folder + "/" + entry.name;
    std::string detail = human_time(static_cast<time_t>(entry.modified));
    const std::string size = human_size(folder_size(path));
    detail = detail.empty() ? size : detail + " · " + size;
    result.push_back({ entry.name, path, detail });
  }
  return result;
}

// The selector writes tw_restore, and readBackup reads the folder.
bool twrp_restore_backend::open(const std::string& path) {
  if (running_.load() || path.empty()) return false;

  DataManager::SetValue("tw_restore", path);
  std::string Restore_Name;
  DataManager::GetValue("tw_restore", Restore_Name);
  PartitionManager.SetRestoreFiles(Restore_Name);
  opened_ = path;

  std::string list;
  DataManager::GetValue("tw_restore_list", list);
  return !list.empty();
}

std::vector<restore_target> twrp_restore_backend::targets() {
  if (opened_.empty()) return {};

  std::vector<PartitionList> list;
  PartitionManager.GetPartitionList("restore", &list);

  std::vector<restore_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.display_name, entry.mount_point });
  return result;
}

bool twrp_restore_backend::encrypted() {
  int value = 0;
  DataManager::GetValue("tw_restore_encrypted", value);
  return value != 0;
}

std::string twrp_restore_backend::date() {
  std::string value;
  DataManager::GetValue(TW_RESTORE_FILE_DATE, value);
  // The variable holds ctime output, newline and all.
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
  return value;
}

// restore_decrypt writes tw_restore_password; decrypt_backup checks it, and
// the restore itself reads it again for every encrypted archive.
bool twrp_restore_backend::unlock(const std::string& password) {
  if (opened_.empty() || password.empty()) return false;
  DataManager::SetValue("tw_restore_password", password);

  int op_status = 0;

  operation_start("Try Restore Decrypt");
  {
    std::string Restore_Path, Filename, Password;
    DataManager::GetValue("tw_restore", Restore_Path);
    Restore_Path += "/";
    DataManager::GetValue("tw_restore_password", Password);
    TWFunc::SetPerformanceMode(true);
    if (TWFunc::TryDecryptingBackup(Restore_Path, Password))
      op_status = 0;  // success
    else
      op_status = 1;  // fail
    TWFunc::SetPerformanceMode(false);
  }

  operation_end(op_status);
  return op_status == 0;
}

bool twrp_restore_backend::start(const std::vector<std::string>& mount_points) {
  if (running_.load() || opened_.empty() || mount_points.empty()) return false;
  join_finished_thread();

  std::string list;
  for (const std::string& mount_point : mount_points) {
    list += mount_point;
    list += ';';
  }
  DataManager::SetValue("tw_restore_selected", list);
  DataManager::SetValue("tw_size_progress", "");
  DataManager::SetValue("tw_file_progress", "");

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = restore_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_restore_backend::run, this);
  return true;
}

// GUIAction::nandroid("restore")
void twrp_restore_backend::run() {
  operation_start("Nandroid");
  int ret = 0;

  std::string Restore_Name;
  DataManager::GetValue("tw_restore", Restore_Name);
  ret = PartitionManager.RunRestore(Restore_Name) ? 0 : 1;
  operation_end(ret);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ret == 0 ? restore_state::DONE : restore_state::FAILED;
  }
  running_.store(false);
}

restore_status twrp_restore_backend::status() {
  restore_status result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result.state = state_;
  }
  if (result.state == restore_state::RUNNING) {
    std::string detail;
    DataManager::GetValue("tw_size_progress", detail);
    if (detail.empty()) DataManager::GetValue("tw_file_progress", detail);
    result.detail = detail;
  }
  return result;
}

void twrp_restore_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != restore_state::RUNNING) state_ = restore_state::IDLE;
}

}  // namespace gui2_backend
