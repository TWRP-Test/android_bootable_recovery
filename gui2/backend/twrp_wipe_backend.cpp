#include "twrp_wipe_backend.h"

#include <utility>

#include "data.hpp"
#include "gui/gui.hpp"
#include "gui/twmsg.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp_operation.h"
#include "variables.h"

namespace gui2_backend {
namespace {

// GUIAction::wipe; 0 is success.
int wipe(std::string arg) {
  operation_start("Format");
  DataManager::SetValue("tw_partition", arg);
  int ret_val = false;

  if (arg == "data")
    ret_val = PartitionManager.Factory_Reset();
  else if (arg == "battery")
    ret_val = PartitionManager.Wipe_Battery_Stats();
  else if (arg == "rotate")
    ret_val = PartitionManager.Wipe_Rotate_Data();
  else if (arg == "dalvik")
    ret_val = PartitionManager.Wipe_Dalvik_Cache();
  else if (arg == "DATAMEDIA") {
    ret_val = PartitionManager.Format_Data();
  } else if (arg == "INTERNAL") {
    int has_datamedia;

    DataManager::GetValue(TW_HAS_DATA_MEDIA, has_datamedia);
    if (has_datamedia) {
      ret_val = PartitionManager.Wipe_Media_From_Data();
    } else {
      ret_val = PartitionManager.Wipe_By_Path(DataManager::GetCurrentStoragePath());
    }
  } else if (arg == "EXTERNAL") {
    std::string External_Path;

    DataManager::GetValue(TW_EXTERNAL_PATH, External_Path);
    ret_val = PartitionManager.Wipe_By_Path(External_Path);
  } else if (arg == "ANDROIDSECURE") {
    ret_val = PartitionManager.Wipe_Android_Secure();
  } else if (arg == "LIST") {
    std::string Wipe_List, wipe_path;
    bool skip = false;
    ret_val = true;

    DataManager::GetValue("tw_wipe_list", Wipe_List);
    LOGINFO("wipe list '%s'\n", Wipe_List.c_str());
    if (!Wipe_List.empty()) {
      size_t start_pos = 0, end_pos = Wipe_List.find(";", start_pos);
      while (end_pos != std::string::npos && start_pos < Wipe_List.size()) {
        wipe_path = Wipe_List.substr(start_pos, end_pos - start_pos);
        LOGINFO("wipe_path '%s'\n", wipe_path.c_str());
        if (wipe_path == "/and-sec") {
          if (!PartitionManager.Wipe_Android_Secure()) {
            gui_msg("and_sec_wipe_err=Unable to wipe android secure");
            ret_val = false;
            break;
          } else {
            skip = true;
          }
        } else if (wipe_path == "DALVIK") {
          if (!PartitionManager.Wipe_Dalvik_Cache()) {
            gui_err("dalvik_wipe_err=Failed to wipe dalvik");
            ret_val = false;
            break;
          } else {
            skip = true;
          }
        } else if (wipe_path == "INTERNAL") {
          if (!PartitionManager.Wipe_Media_From_Data()) {
            ret_val = false;
            break;
          } else {
            skip = true;
          }
        }
        if (!skip) {
          if (!PartitionManager.Wipe_By_Path(wipe_path)) {
            gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(wipe_path));
            ret_val = false;
            break;
          } else if (wipe_path == DataManager::GetCurrentStoragePath()) {
            arg = wipe_path;
          }
        } else {
          skip = false;
        }
        start_pos = end_pos + 1;
        end_pos = Wipe_List.find(";", start_pos);
      }
    }
  } else
    ret_val = PartitionManager.Wipe_By_Path(arg);
  PartitionManager.Update_System_Details();
  if (ret_val)
    ret_val = 0;  // 0 is success
  else
    ret_val = 1;  // 1 is failure
  operation_end(ret_val);
  return ret_val;
}

}  // namespace

twrp_wipe_backend::~twrp_wipe_backend() {
  if (worker_.joinable()) worker_.join();
}

std::vector<wipe_target> twrp_wipe_backend::targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("wipe", &list);

  std::vector<wipe_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.Display_Name, entry.Mount_Point });
  return result;
}

bool twrp_wipe_backend::has_data_media() {
  return settings_ != nullptr && settings_->get_int(TW_HAS_DATA_MEDIA, 0) != 0;
}

void twrp_wipe_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_wipe_backend::start(std::vector<std::string> args) {
  if (running_.load() || args.empty()) return false;
  join_finished_thread();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = wipe_state::RUNNING;
    status_.done = 0;
    status_.total = 0;
  }

  running_.store(true);
  worker_ = std::thread(&twrp_wipe_backend::run, this, std::move(args));
  return true;
}

bool twrp_wipe_backend::start_factory_reset() {
  return start({ "data" });
}

bool twrp_wipe_backend::start_format_data() {
  return start({ "DATAMEDIA" });
}

// The advanced wipe page: its list writes tw_wipe_list, "path;" per row.
bool twrp_wipe_backend::start_wipe(const std::vector<std::string>& mount_points) {
  std::string list;
  for (const std::string& path : mount_points) list += path + ";";
  DataManager::SetValue("tw_wipe_list", list);
  return start({ "LIST" });
}

// Each argument is one wipe action; the page shows how the last one ended.
void twrp_wipe_backend::run(std::vector<std::string> args) {
  int ret_val = 0;
  for (const std::string& arg : args) ret_val = wipe(arg);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = ret_val == 0 ? wipe_state::DONE : wipe_state::FAILED;
  }
  running_.store(false);
}

wipe_status twrp_wipe_backend::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

}  // namespace gui2_backend
