#include "twrp_backup_backend.h"

#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"

namespace gui2_backend {

twrp_backup_backend::~twrp_backup_backend() {
  if (worker_.joinable()) {
    PartitionManager.stop_backup = true;
    worker_.join();
  }
}

std::vector<backup_target> twrp_backup_backend::targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("backup", &list);

  std::vector<backup_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.Display_Name, entry.Mount_Point });
  return result;
}

void twrp_backup_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_backup_backend::start(const std::vector<std::string>& mount_points,
                                const std::string& name, bool compress, bool skip_digest,
                                bool encrypt, const std::string& password) {
  if (running_.load() || mount_points.empty()) return false;
  join_finished_thread();

  std::string list;
  for (const std::string& mount_point : mount_points) {
    list += mount_point;
    list += ';';
  }
  DataManager::SetValue("tw_backup_list", list);
  DataManager::SetValue(TW_BACKUP_NAME, name.empty() ? "(Current Date)" : name);
  DataManager::SetValue(TW_USE_COMPRESSION_VAR, compress ? 1 : 0);
  DataManager::SetValue(TW_SKIP_DIGEST_GENERATE_VAR, skip_digest ? 1 : 0);
  const bool encrypting = encrypt && !password.empty();
  DataManager::SetValue("tw_encrypt_backup", encrypting ? 1 : 0);
  if (encrypting) DataManager::SetValue("tw_backup_password", password);
  DataManager::SetValue("tw_size_progress", "");
  DataManager::SetValue("tw_file_progress", "");
  PartitionManager.stop_backup = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = backup_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_backup_backend::run, this);
  return true;
}

void twrp_backup_backend::run() {
  const bool ok = PartitionManager.Run_Backup(false);
  const bool cancelled = PartitionManager.Check_Backup_Cancel() != 0;

  // The legacy UI clears this so the next backup is not encrypted by accident.
  DataManager::SetValue("tw_encrypt_backup", 0);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled)
      state_ = backup_state::CANCELLED;
    else
      state_ = ok ? backup_state::DONE : backup_state::FAILED;
  }
  running_.store(false);
}

void twrp_backup_backend::cancel() {
  if (running_.load()) PartitionManager.stop_backup = true;
}

backup_status twrp_backup_backend::status() {
  backup_status result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result.state = state_;
  }
  if (result.state == backup_state::RUNNING) {
    std::string detail;
    DataManager::GetValue("tw_size_progress", detail);
    if (detail.empty()) DataManager::GetValue("tw_file_progress", detail);
    result.detail = detail;
  }
  return result;
}

void twrp_backup_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != backup_state::RUNNING) state_ = backup_state::IDLE;
}

}  // namespace gui2_backend
