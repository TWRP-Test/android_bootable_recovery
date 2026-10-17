#include "twrp_wipe_backend.h"

#include <utility>
#include <functional>

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
int wipe(std::string arg, const std::function<void(const std::string&, int, bool)>& report) {
  operation_start("Format");
  DataManager::SetValue("tw_partition", arg);
  int ret_val = false;

  if (arg == "data")
    ret_val = PartitionManager.FactoryReset();
  else if (arg == "battery")
    ret_val = PartitionManager.WipeBatteryStats();
  else if (arg == "rotate")
    ret_val = PartitionManager.WipeRotateData();
  else if (arg == "dalvik")
    ret_val = PartitionManager.WipeDalvikCache();
  else if (arg == "DATAMEDIA") {
    ret_val = PartitionManager.FormatData();
  } else if (arg == "INTERNAL") {
    int has_datamedia;

    DataManager::GetValue(TW_HAS_DATA_MEDIA, has_datamedia);
    if (has_datamedia) {
      ret_val = PartitionManager.WipeMediaFromData();
    } else {
      ret_val = PartitionManager.WipeByPath(DataManager::GetCurrentStoragePath());
    }
  } else if (arg == "EXTERNAL") {
    std::string External_Path;

    DataManager::GetValue(TW_EXTERNAL_PATH, External_Path);
    ret_val = PartitionManager.WipeByPath(External_Path);
  } else if (arg == "ANDROIDSECURE") {
    ret_val = PartitionManager.WipeAndroidSecure();
  } else if (arg == "LIST") {
    std::string Wipe_List, wipe_path;
    bool skip = false;
    ret_val = true;

    int item_index = 0;
    DataManager::GetValue("tw_wipe_list", Wipe_List);
    LOGINFO("wipe list '%s'\n", Wipe_List.c_str());
    if (!Wipe_List.empty()) {
      size_t start_pos = 0, end_pos = Wipe_List.find(";", start_pos);
      while (end_pos != std::string::npos && start_pos < Wipe_List.size()) {
        wipe_path = Wipe_List.substr(start_pos, end_pos - start_pos);
        LOGINFO("wipe_path '%s'\n", wipe_path.c_str());
        report(wipe_path, item_index, false);
        if (wipe_path == "/and-sec") {
          if (!PartitionManager.WipeAndroidSecure()) {
            gui_msg("and_sec_wipe_err=Unable to wipe android secure");
            ret_val = false;
            break;
          } else {
            skip = true;
          }
        } else if (wipe_path == "DALVIK") {
          if (!PartitionManager.WipeDalvikCache()) {
            gui_err("dalvik_wipe_err=Failed to wipe dalvik");
            ret_val = false;
            break;
          } else {
            skip = true;
          }
        } else if (wipe_path == "INTERNAL") {
          if (!PartitionManager.WipeMediaFromData()) {
            ret_val = false;
            break;
          } else {
            skip = true;
          }
        }
        if (!skip) {
          if (!PartitionManager.WipeByPath(wipe_path)) {
            gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(wipe_path));
            ret_val = false;
            break;
          } else if (wipe_path == DataManager::GetCurrentStoragePath()) {
            arg = wipe_path;
          }
        } else {
          skip = false;
        }
        report(wipe_path, item_index++, true);
        start_pos = end_pos + 1;
        end_pos = Wipe_List.find(";", start_pos);
      }
    }
  } else
    ret_val = PartitionManager.WipeByPath(arg);
  PartitionManager.UpdateSystemDetails();
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
  PartitionManager.GetPartitionList("wipe", &list);

  std::vector<wipe_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.display_name, entry.mount_point });
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
    status_ = {};
    status_.state = wipe_state::RUNNING;
    if (args.front() == "LIST") {
      const std::string list = DataManager::GetStrValue("tw_wipe_list");
      size_t start = 0;
      for (size_t end; (end = list.find(';', start)) != std::string::npos; start = end + 1)
        if (end > start) status_.items.push_back(list.substr(start, end - start));
    } else {
      status_.items = args;
    }
    status_.total = static_cast<int>(status_.items.size());
    started_ = std::chrono::steady_clock::now();
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

void twrp_wipe_backend::report_item(const std::string&, int index, bool completed) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.current = index;
  if (completed) status_.done = index + 1;
}

// Keep the existing LIST operation and its final size update intact.
void twrp_wipe_backend::run(std::vector<std::string> args) {
  int ret_val = 0;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg != "LIST") report_item(arg, static_cast<int>(i), false);
    ret_val = wipe(arg, [this](const std::string& path, int index, bool completed) {
      report_item(path, index, completed);
    });
    if (ret_val != 0) break;
    if (arg != "LIST") report_item(arg, static_cast<int>(i), true);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = ret_val == 0 ? wipe_state::DONE : wipe_state::FAILED;
    status_.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_).count();
  }
  running_.store(false);
}

wipe_status twrp_wipe_backend::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto result = status_;
  if (running_.load())
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_).count();
  return result;
}

}  // namespace gui2_backend
