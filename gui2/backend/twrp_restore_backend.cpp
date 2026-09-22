#include "twrp_restore_backend.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
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

std::vector<restore_backup> twrp_restore_backend::backups() {
  std::string folder;
  DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, folder);
  if (folder.empty()) return {};

  DIR* directory = opendir(folder.c_str());
  if (directory == nullptr) return {};

  struct dated_backup {
    restore_backup backup;
    time_t when;
  };
  std::vector<dated_backup> found;

  while (const dirent* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;

    const std::string path = folder + "/" + name;
    struct stat info;
    if (lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) continue;

    std::string detail = human_time(info.st_mtime);
    const std::string size = human_size(folder_size(path));
    if (detail.empty())
      detail = size;
    else
      detail += " · " + size;
    found.push_back({ { name, path, detail }, info.st_mtime });
  }
  closedir(directory);

  // Newest first: the backup someone wants back is almost always the last one.
  std::sort(found.begin(), found.end(), [](const dated_backup& a, const dated_backup& b) {
    if (a.when != b.when) return a.when > b.when;
    return a.backup.name > b.backup.name;
  });

  std::vector<restore_backup> result;
  result.reserve(found.size());
  for (dated_backup& entry : found) result.push_back(std::move(entry.backup));
  return result;
}

bool twrp_restore_backend::open(const std::string& path) {
  if (running_.load() || path.empty()) return false;

  // Set_Restore_Files fills tw_restore_list, tw_restore_encrypted and the date,
  // which is everything the rest of this class reports.
  DataManager::SetValue("tw_restore", path);
  PartitionManager.Set_Restore_Files(path);
  opened_ = path;

  std::string list;
  DataManager::GetValue("tw_restore_list", list);
  return !list.empty();
}

std::vector<restore_target> twrp_restore_backend::targets() {
  if (opened_.empty()) return {};

  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("restore", &list);

  std::vector<restore_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.Display_Name, entry.Mount_Point });
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

bool twrp_restore_backend::unlock(const std::string& password) {
  if (opened_.empty() || password.empty()) return false;

  TWFunc::SetPerformanceMode(true);
  const bool ok = TWFunc::Try_Decrypting_Backup(opened_ + "/", password);
  TWFunc::SetPerformanceMode(false);
  if (!ok) return false;

  // Decrypting rewrites the archives in place, so the folder has to be read
  // again before anything is restored out of it.
  PartitionManager.Set_Restore_Files(opened_);
  return true;
}

bool twrp_restore_backend::start(const std::vector<std::string>& mount_points, bool check_digest) {
  if (running_.load() || opened_.empty() || mount_points.empty()) return false;
  join_finished_thread();

  std::string list;
  for (const std::string& mount_point : mount_points) {
    list += mount_point;
    list += ';';
  }
  DataManager::SetValue("tw_restore_selected", list);
  DataManager::SetValue(TW_SKIP_DIGEST_CHECK_VAR, check_digest ? 1 : 0);
  DataManager::SetValue("tw_size_progress", "");
  DataManager::SetValue("tw_file_progress", "");

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = restore_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_restore_backend::run, this, opened_);
  return true;
}

void twrp_restore_backend::run(std::string path) {
  TWFunc::SetPerformanceMode(true);
  const bool ok = PartitionManager.Run_Restore(path) != 0;
  TWFunc::SetPerformanceMode(false);
  PartitionManager.Update_System_Details();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ok ? restore_state::DONE : restore_state::FAILED;
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
