#include "twrp_mount_backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <android-base/properties.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "twrp_operation.h"
#include "variables.h"

namespace gui2_backend {

// GUIAction::mountsystemtoggle
int mountsystemtoggle(const std::string& arg) {
  int op_status = 0;
  bool remount_system = PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path());
  bool remount_vendor = PartitionManager.Is_Mounted_By_Path("/vendor");

  operation_start("Toggle System Mount");
  if (!PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), true)) {
    op_status = 1;  // fail
  } else {
    TWPartition* Part = PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
    if (Part) {
      if (arg == "0") {
        DataManager::SetValue("tw_mount_system_ro", 0);
        Part->Change_Mount_Read_Only(false);
      } else {
        DataManager::SetValue("tw_mount_system_ro", 1);
        Part->Change_Mount_Read_Only(true);
      }
      if (remount_system) {
        Part->Mount(true);
      }
      op_status = 0;  // success
    } else {
      op_status = 1;  // fail
    }
    Part = PartitionManager.Find_Partition_By_Path("/vendor");
    if (Part) {
      if (arg == "0") {
        Part->Change_Mount_Read_Only(false);
      } else {
        Part->Change_Mount_Read_Only(true);
      }
      if (remount_vendor) {
        Part->Mount(true);
      }
      op_status = 0;  // success
    } else {
      op_status = 1;  // fail
    }
  }

  operation_end(op_status);
  return op_status;
}

static std::string otg_name(const std::string& path) {
  if (!is_otg_path(path)) return {};
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("mount", &list);
  std::vector<std::string> paths;
  for (const auto& item : list)
    if (is_otg_path(item.Mount_Point)) paths.push_back(item.Mount_Point);
  std::sort(paths.begin(), paths.end());
  if (paths.size() <= 1) return "OTG";
  const auto found = std::find(paths.begin(), paths.end(), path);
  return found == paths.end() ? "OTG" :
      "OTG " + std::to_string(std::distance(paths.begin(), found) + 1);
}

std::vector<mount_target> twrp_mount_backend::targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("mount", &list);

  std::vector<mount_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ is_otg_path(entry.Mount_Point) ? otg_name(entry.Mount_Point) : entry.Display_Name,
                       entry.Mount_Point, entry.selected });
  return result;
}

// GUIPartitionList::NotifySelect for the "mount" list.
bool twrp_mount_backend::set_mounted(const std::string& mount_point, bool mounted) {
  if (mounted) {
    if (PartitionManager.Mount_By_Path(mount_point, true)) {
      PartitionManager.Add_MTP_Storage(mount_point);
      return true;
    }
    return false;
  }
  return PartitionManager.UnMount_By_Path(mount_point, true) != 0;
}

bool twrp_mount_backend::system_toggle_visible() {
  return DataManager::GetIntValue("tw_is_super") == 0;
}

bool twrp_mount_backend::system_writable() {
  return DataManager::GetIntValue("tw_mount_system_ro") == 0;
}

bool twrp_mount_backend::set_system_writable(bool writable) {
  return mountsystemtoggle(writable ? "0" : "1") == 0;
}

// system_readonly_check: GUIAction::checkpartitionlifetimewrites("/system").
// A system that was never written to gets the system_readonly page first.
bool twrp_mount_backend::system_needs_warning() {
  int op_status = 0;
  TWPartition* sys = PartitionManager.Find_Partition_By_Path("/system");

  operation_start("Check Partition Lifetime Writes");
  if (sys) {
    if (sys->Check_Lifetime_Writes() != 0)
      DataManager::SetValue("tw_lifetime_writes", 1);
    else
      DataManager::SetValue("tw_lifetime_writes", 0);
    op_status = 0;  // success
  } else {
    DataManager::SetValue("tw_lifetime_writes", 1);
    op_status = 1;  // fail
  }

  operation_end(op_status);
  return DataManager::GetIntValue("tw_lifetime_writes") == 0;
}

std::vector<storage_device> twrp_mount_backend::storages() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("storage", &list);

  std::vector<storage_device> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ is_otg_path(entry.Mount_Point) ? otg_name(entry.Mount_Point) : entry.Display_Name,
                       entry.Mount_Point, entry.selected });
  return result;
}

// GUIPartitionList::NotifySelect for the "storage" list, which then sets
// tw_storage_path; that is what recomputes the backup folder, the display name
// and the free size.
bool twrp_mount_backend::select_storage(const std::string& path) {
  TWPartition* Part = PartitionManager.Find_Partition_By_Path(path);
  if (Part == nullptr) {
    LOGERR("Unable to locate partition for '%s'\n", path.c_str());
    return false;
  }
  bool update_size = !Part->Is_Mounted() && Part->Removable;
  if (!Part->Mount(true)) return false;
  if (update_size && !Part->Update_Size(true)) return false;
  DataManager::SetValue("tw_storage_path", path);
  return true;
}

std::string twrp_mount_backend::storage_name() {
  std::string value;
  DataManager::GetValue("tw_storage_display_name", value);
  return is_otg_path(DataManager::GetCurrentStoragePath()) ?
      otg_name(DataManager::GetCurrentStoragePath()) : value;
}

std::string twrp_mount_backend::storage_free() {
  std::string value;
  DataManager::GetValue("tw_storage_free_size", value);
  return value;
}

bool twrp_mount_backend::has_mtp() {
  return DataManager::GetIntValue("tw_has_mtp") != 0;
}

bool twrp_mount_backend::mtp_enabled() {
  return DataManager::GetIntValue("tw_mtp_enabled") != 0;
}

// GUIAction::startmtp / stopmtp
bool twrp_mount_backend::set_mtp_enabled(bool enabled) {
  int op_status = 0;
  if (enabled) {
    operation_start("Start MTP");
    if (PartitionManager.Enable_MTP())
      op_status = 0;  // success
    else
      op_status = 1;  // fail
  } else {
    operation_start("Stop MTP");
    if (PartitionManager.Disable_MTP())
      op_status = 0;  // success
    else
      op_status = 1;  // fail
  }
  operation_end(op_status);
  return op_status == 0;
}

bool twrp_mount_backend::has_usb_storage() {
  if (DataManager::GetIntValue(TW_HAS_USB_STORAGE) == 0) return false;
  // A configfs function directory alone does not configure or bind a gadget.
  // Check the installed device rules without switching USB mode to probe it.
  std::ifstream rules("/init.recovery.usb.rc");
  std::ostringstream text;
  text << rules.rdbuf();
  const std::string config = text.str();
  if (config.find("sys.usb.config=mass_storage,adb") == std::string::npos) return false;
  if (android::base::GetProperty("sys.usb.configfs", "0") == "1")
    return config.find("symlink /config/usb_gadget/g1/functions/mass_storage.") !=
               std::string::npos &&
           config.find("write /config/usb_gadget/g1/UDC") != std::string::npos;
  return config.find("/sys/class/android_usb/android0/functions") != std::string::npos;
}

bool twrp_mount_backend::usb_storage_enabled() {
  return usb_storage_on_;
}

// GUIAction::mount("usb") / unmount("usb"), which hold tw_busy while the
// storage is out on the computer.
bool twrp_mount_backend::set_usb_storage_enabled(bool enabled) {
  if (enabled) {
    DataManager::SetValue(TW_ACTION_BUSY, 1);
    if (!has_usb_storage() || !PartitionManager.usb_storage_enable()) {
      DataManager::SetValue(TW_ACTION_BUSY, 0);
      return false;
    }
    // The partition manager requests the switch; init acknowledges it.
    if (!android::base::WaitForProperty("sys.usb.state", "mass_storage,adb",
                                        std::chrono::milliseconds(1500))) {
      PartitionManager.usb_storage_disable();
      DataManager::SetValue(TW_ACTION_BUSY, 0);
      return false;
    }
  } else {
    if (!PartitionManager.usb_storage_disable()) return false;
    DataManager::SetValue(TW_ACTION_BUSY, 0);
  }
  usb_storage_on_ = enabled;
  return true;
}

}  // namespace gui2_backend
