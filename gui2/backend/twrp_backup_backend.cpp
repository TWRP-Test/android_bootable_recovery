#include "twrp_backup_backend.h"

#include "data.hpp"
#include "gui/gui.hpp"
#include "gui/twmsg.h"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "twrp_operation.h"
#include "variables.h"

namespace gui2_backend {

twrp_backup_backend::~twrp_backup_backend() {
  if (cancel_worker_.joinable()) cancel_worker_.join();
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

// generatebackupname
std::string twrp_backup_backend::generate_name() {
  operation_start("GenerateBackupName");
  TWFunc::Auto_Generate_Backup_Name();
  operation_end(0);
  return DataManager::GetStrValue(TW_BACKUP_NAME);
}

// appenddatetobackupname, on the name as the page holds it.
std::string twrp_backup_backend::append_date(const std::string& name) {
  operation_start("AppendDateToBackupName");
  std::string Backup_Name = name;
  Backup_Name += TWFunc::Get_Current_Date();
  if (Backup_Name.size() > MAX_BACKUP_NAME_LEN) Backup_Name.resize(MAX_BACKUP_NAME_LEN);
  DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
  operation_end(0);
  return Backup_Name;
}

void twrp_backup_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_backup_backend::start() {
  if (running_.load()) return false;
  join_finished_thread();
  DataManager::SetValue("tw_size_progress", "");
  DataManager::SetValue("tw_file_progress", "");

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = backup_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_backup_backend::run, this);
  return true;
}

// GUIAction::nandroid("backup")
void twrp_backup_backend::run() {
  operation_start("Nandroid");
  int ret = 0;
  bool name_ok = true;

  std::string Backup_Name;
  DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
  std::string auto_gen = gui_lookup("auto_generate", "(Auto Generate)");
  if (Backup_Name == auto_gen || Backup_Name == gui_lookup("curr_date", "(Current Date)") ||
      Backup_Name == "0" || Backup_Name == "(" ||
      PartitionManager.Check_Backup_Name(Backup_Name, true, true) == 0) {
    ret = PartitionManager.Run_Backup(false);
    DataManager::SetValue("tw_encrypt_backup", 0);  // reset value so we don't encrypt every subsequent backup
    if (!PartitionManager.stop_backup) {
      if (ret == false)
        ret = 1;  // 1 for failure
      else
        ret = 0;  // 0 for success
      DataManager::SetValue("tw_cancel_backup", 0);
    } else {
      DataManager::SetValue("tw_cancel_backup", 1);
      gui_msg("backup_cancel=Backup Cancelled");
      ret = 0;
    }
  } else {
    operation_end(1);
    name_ok = false;
  }
  if (name_ok) {
    DataManager::SetValue(TW_BACKUP_NAME, auto_gen);
    operation_end(ret);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!name_ok || ret != 0)
      state_ = backup_state::FAILED;
    else if (DataManager::GetIntValue("tw_cancel_backup") != 0)
      state_ = backup_state::CANCELLED;
    else
      state_ = backup_state::DONE;
  }
  running_.store(false);
}

// cancelbackup, which the legacy UI runs on its cancel thread: it waits for
// tar to stop.
void twrp_backup_backend::cancel() {
  if (!running_.load() || cancelling_.exchange(true)) return;
  if (cancel_worker_.joinable()) cancel_worker_.join();
  cancel_worker_ = std::thread([this] {
    PartitionManager.Cancel_Backup();
    cancelling_.store(false);
  });
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
  if (!running_.load() && cancel_worker_.joinable()) cancel_worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != backup_state::RUNNING) state_ = backup_state::IDLE;
}

}  // namespace gui2_backend
