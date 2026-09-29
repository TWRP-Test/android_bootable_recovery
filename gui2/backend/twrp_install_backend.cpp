#include "twrp_install_backend.h"

#include <sys/mount.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <cutils/properties.h>

#include "data.hpp"
#include "gui/gui.hpp"
#include "gui/twmsg.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twinstall.h"
#include "twrp-functions.hpp"
#include "twrp_operation.h"
#include "variables.h"

namespace gui2_backend {
namespace {

#ifdef TW_OZIP_DECRYPT_KEY
int ozip_decrypt(std::string zip_path) {
  if (!TWFunc::Path_Exists("/system/bin/ozip_decrypt")) {
    return 1;
  }
  gui_msg("ozip_decrypt_decryption=Starting Ozip Decryption...");
  TWFunc::Exec_Cmd("ozip_decrypt " + (std::string)TW_OZIP_DECRYPT_KEY + " '" + zip_path + "'");
  gui_msg("ozip_decrypt_finish=Ozip Decryption Finished!");
  return 0;
}
#endif

// GUIAction::flash_zip
int flash_zip(std::string filename, int* wipe_cache) {
  int ret_val = 0;

  DataManager::SetValue("ui_progress", 0);
  DataManager::SetValue("ui_portion_size", 0);
  DataManager::SetValue("ui_portion_start", 0);

  if (filename.empty()) {
    LOGERR("No file specified.\n");
    return -1;
  }

  if (!TWFunc::Path_Exists(filename)) {
    if (!PartitionManager.Mount_By_Path(filename, true)) {
      return -1;
    }
    if (!TWFunc::Path_Exists(filename)) {
      gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")(filename));
      return -1;
    }
  }

  char apex_enabled[PROPERTY_VALUE_MAX];
  property_get("twrp.apex.flattened", apex_enabled, "");
  if (strcmp(apex_enabled, "true") == 0) {
    umount("/apex");
  }
  ret_val = TWinstall_zip(filename.c_str(), wipe_cache,
                          (bool)!DataManager::GetIntValue(TW_SKIP_DIGEST_CHECK_ZIP_VAR));
  PartitionManager.Unlock_Block_Partitions();
  // Now, check if we need to ensure TWRP remains installed...
  struct stat st;
  if (stat("/system/bin/installTwrp", &st) == 0) {
    DataManager::SetValue("tw_operation", "Configuring TWRP");
    DataManager::SetValue("tw_partition", "");
    gui_msg("config_twrp=Configuring TWRP...");
    if (TWFunc::Exec_Cmd("/system/bin/installTwrp reinstall") < 0) {
      gui_msg("config_twrp_err=Unable to configure TWRP with this kernel.");
    }
  }

  // Done
  DataManager::SetValue("ui_progress", 100);
  DataManager::SetValue("ui_progress", 0);
  DataManager::SetValue("ui_portion_size", 0);
  DataManager::SetValue("ui_portion_start", 0);
  return ret_val;
}

}  // namespace

twrp_install_backend::~twrp_install_backend() {
  if (worker_.joinable()) worker_.join();
}

void twrp_install_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_install_backend::start_zip(const std::vector<std::string>& paths) {
  if (running_.load() || paths.empty()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = install_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_install_backend::run_zip, this, paths);
  return true;
}

// GUIAction::flash; the queue is gui2's, handed over as it is.
void twrp_install_backend::run_zip(std::vector<std::string> zip_queue) {
  int i, ret_val = 0, wipe_cache = 0;
  const int zip_queue_index = static_cast<int>(zip_queue.size());
  for (i = 0; i < zip_queue_index; i++) {
    std::string zip_path = zip_queue[i];
    size_t slashpos = zip_path.find_last_of('/');
    std::string zip_filename =
        (slashpos == std::string::npos) ? zip_path : zip_path.substr(slashpos + 1);
    operation_start("Flashing");
#ifdef TW_OZIP_DECRYPT_KEY
    if ((zip_path.substr(zip_path.size() - 4, 4)) == "ozip") {
      if ((ozip_decrypt(zip_path)) != 0) {
        LOGERR("Unable to find ozip_decrypt!");
        break;
      }
      zip_filename = (zip_filename.substr(0, zip_filename.size() - 4)).append("zip");
      zip_path = (zip_path.substr(0, zip_path.size() - 4)).append("zip");
      if (!TWFunc::Path_Exists(zip_path)) {
        LOGERR("Unable to find decrypted zip");
        break;
      }
    }
#endif
    DataManager::SetValue("tw_filename", zip_path);
    DataManager::SetValue("tw_file", zip_filename);
    DataManager::SetValue(TW_ZIP_INDEX, (i + 1));

    TWFunc::SetPerformanceMode(true);
    ret_val = flash_zip(zip_path, &wipe_cache);
    TWFunc::SetPerformanceMode(false);
    if (ret_val != 0) {
      gui_msg(Msg(msg::kError, "zip_err=Error installing zip file '{1}'")(zip_path));
      ret_val = 1;
      break;
    }
  }

  if (wipe_cache) {
    gui_msg("zip_wipe_cache=One or more zip requested a cache wipe -- Wiping cache now.");
    PartitionManager.Wipe_By_Path("/cache");
  }

  PartitionManager.Update_System_Details();
  operation_end(ret_val);
  DataManager::SetValue(TW_ZIP_QUEUE_COUNT, 0);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ret_val == 0 ? install_state::DONE : install_state::FAILED;
  }
  running_.store(false);
}

std::vector<image_target> twrp_install_backend::image_targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("flashimg", &list);

  std::vector<image_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list) {
    TWPartition* partition = PartitionManager.Find_Partition_By_Path(entry.Mount_Point);
    result.push_back({ entry.Display_Name, entry.Mount_Point,
                       partition != nullptr && partition->Is_SlotSelect() });
  }
  return result;
}

// GUIFileSelector::NotifySelect for a file ending in .img.
std::string twrp_install_backend::image_target_for(const std::string& filename) {
  std::string str_lower = filename;
  std::transform(str_lower.begin(), str_lower.end(), str_lower.begin(), ::tolower);
  if (str_lower == "boot.img" || str_lower == "boot_a.img" || str_lower == "boot_b.img") {
    return "/boot";
  } else if (str_lower == "init_boot.img" || str_lower == "init_boot_a.img" ||
             str_lower == "init_boot_b.img") {
    return "/init_boot";
  } else if (str_lower == "vendor_boot.img" || str_lower == "vendor_boot_a.img" ||
             str_lower == "vendor_boot_b.img") {
    return "/vendor_boot";
  } else if ((str_lower.find("recovery") == 0) || (str_lower.find("twrp") == 0) ||
             (str_lower.find("orangefox") == 0)) {
    return "/recovery";
  } else if (str_lower == "dtbo.img" || str_lower == "dtbo_a.img" || str_lower == "dtbo_b.img") {
    return "/dtbo";
  } else if (str_lower.find("kernelsu_patched_") == 0) {
    if (PartitionManager.Find_Partition_By_Path("/init_boot") != nullptr) {
      return "/init_boot";
    } else {
      return "/boot";
    }
  }
  return "";
}

bool twrp_install_backend::start_image(const std::string& path, const std::string& mount_point,
                                       bool both_slots) {
  if (running_.load() || path.empty()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = install_state::RUNNING;
  }
  // What the legacy file selector and flash image page leave behind.
  const size_t slash = path.find_last_of('/');
  DataManager::SetValue("tw_zip_location", slash == std::string::npos ? "/" : path.substr(0, slash));
  DataManager::SetValue("tw_file", slash == std::string::npos ? path : path.substr(slash + 1));
  DataManager::SetValue("tw_flash_partition", mount_point + ";");
  DataManager::SetValue("tw_flash_both_slots", both_slots ? 1 : 0);
  running_.store(true);
  worker_ = std::thread(&twrp_install_backend::run_image, this);
  return true;
}

// GUIAction::flashimage
void twrp_install_backend::run_image() {
  int op_status = 0;
  bool flag = true;

  operation_start("Flash Image");
  std::string path, filename;
  DataManager::GetValue("tw_zip_location", path);
  DataManager::GetValue("tw_file", filename);

#ifdef AB_OTA_UPDATER
  std::string target = DataManager::GetStrValue("tw_flash_partition");
  unsigned int pos = target.find_last_of(';');
  std::string mount_point = pos != std::string::npos ? target.substr(0, pos) : "";
  TWPartition* t_part = PartitionManager.Find_Partition_By_Path(mount_point);
  bool flash_in_both_slots = DataManager::GetIntValue("tw_flash_both_slots") ? true : false;

  if (t_part != NULL && (flash_in_both_slots && t_part->Is_SlotSelect())) {
    std::string current_slot = PartitionManager.Get_Active_Slot_Display();
    bool pre_op_status = PartitionManager.Flash_Image(path, filename);

    PartitionManager.Override_Active_Slot(current_slot == "A" ? "B" : "A");
    op_status = (int)!(pre_op_status && PartitionManager.Flash_Image(path, filename));
    PartitionManager.Override_Active_Slot(current_slot);

    DataManager::SetValue("tw_flash_both_slots", 0);
    flag = false;
  }
#endif
  if (flag) {
    if (PartitionManager.Flash_Image(path, filename))
      op_status = 0;  // success
    else
      op_status = 1;  // fail
  }

  operation_end(op_status);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = op_status == 0 ? install_state::DONE : install_state::FAILED;
  }
  running_.store(false);
}

install_status twrp_install_backend::status() {
  install_status result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.state = state_;
  if (state_ == install_state::RUNNING) {
    const int progress = std::atoi(DataManager::GetStrValue("ui_progress").c_str());
    if (progress > 0) result.progress = std::min(progress, 100);
  }
  return result;
}

void twrp_install_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != install_state::RUNNING) state_ = install_state::IDLE;
}

}  // namespace gui2_backend
