#include "twrp_mount_backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <android-base/properties.h>
#include <android-base/scopeguard.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "twrp_operation.h"
#include "twrp_operation_executor.h"
#include "variables.h"

namespace gui2_backend {

// GUIAction::mountsystemtoggle
int mountsystemtoggle(const std::string& arg) {
  int op_status = 0;
  bool remount_system = PartitionManager.IsMountedByPath(PartitionManager.GetAndroidRootPath());
  bool remount_vendor = PartitionManager.IsMountedByPath("/vendor");

  operation_start("Toggle System Mount");
  if (!PartitionManager.UnmountByPath(PartitionManager.GetAndroidRootPath(), true)) {
    op_status = 1;  // fail
  } else {
    TWPartition* Part = PartitionManager.FindPartitionByPath(PartitionManager.GetAndroidRootPath());
    if (Part) {
      if (arg == "0") {
        DataManager::SetValue("tw_mount_system_ro", 0);
        Part->ChangeMountReadOnly(false);
      } else {
        DataManager::SetValue("tw_mount_system_ro", 1);
        Part->ChangeMountReadOnly(true);
      }
      if (remount_system) {
        Part->Mount(true);
      }
      op_status = 0;  // success
    } else {
      op_status = 1;  // fail
    }
    Part = PartitionManager.FindPartitionByPath("/vendor");
    if (Part) {
      if (arg == "0") {
        Part->ChangeMountReadOnly(false);
      } else {
        Part->ChangeMountReadOnly(true);
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

// The numbering of an OTG target is read out of the mount list the caller just
// built, so one page build does not re-snapshot the list for every entry.
static std::string otg_name(const std::string& path, const std::vector<PartitionList>& list) {
  if (!is_otg_path(path)) return {};
  const auto number = path.rfind('-');
  if (number != std::string::npos && number + 1 < path.size() &&
      path.find_first_not_of("0123456789", number + 1) == std::string::npos) {
    const auto disk = path.rfind('-', number - 1);
    if (disk != std::string::npos)
      return "OTG " + path.substr(disk + 1, number - disk - 1) + " " + path.substr(number + 1);
    return "OTG " + path.substr(number + 1);
  }
  std::vector<std::string> paths;
  for (const auto& item : list)
    if (is_otg_path(item.mount_point)) paths.push_back(item.mount_point);
  std::sort(paths.begin(), paths.end());
  if (paths.size() <= 1) return "OTG";
  const auto found = std::find(paths.begin(), paths.end(), path);
  return found == paths.end() ? "OTG" :
      "OTG " + std::to_string(std::distance(paths.begin(), found) + 1);
}

// Numeric partition order within each disk; identities do not depend on which
// other partitions are currently present in the published snapshot.
static void sort_otg_partitions(std::vector<PartitionList>& list) {
  std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
    const bool a_otg = is_otg_path(a.mount_point), b_otg = is_otg_path(b.mount_point);
    if (a_otg != b_otg) return !a_otg;
    if (!a_otg) return false;
    const auto split_a = a.mount_point.rfind('-'), split_b = b.mount_point.rfind('-');
    if (split_a != std::string::npos && split_b != std::string::npos) {
      const auto prefix_a = a.mount_point.substr(0, split_a);
      const auto prefix_b = b.mount_point.substr(0, split_b);
      if (prefix_a != prefix_b) return prefix_a < prefix_b;
      const auto suffix_a = a.mount_point.substr(split_a + 1);
      const auto suffix_b = b.mount_point.substr(split_b + 1);
      if (suffix_a.size() != suffix_b.size()) return suffix_a.size() < suffix_b.size();
      return suffix_a < suffix_b;
    }
    return a.mount_point < b.mount_point;
  });
}

std::vector<mount_target> twrp_mount_backend::targets() {
  std::vector<PartitionList> list;
  PartitionManager.TryGetPartitionList("mount", &list);
  sort_otg_partitions(list);

  std::vector<mount_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ is_otg_path(entry.mount_point) ? otg_name(entry.mount_point, list) :
                       entry.display_name, entry.mount_point, entry.selected });
  return result;
}

// GUIPartitionList::NotifySelect for the "mount" list.
bool twrp_mount_backend::set_mounted(const std::string& mount_point, bool mounted) {
  twrp_operation_executor::guard transaction(twrp_operation_executor::instance().mutex());
  // Publish the actual state before the GUI consumes the worker result.
  const auto publish = android::base::make_scope_guard([] {
    PartitionManager.RefreshPartitionListSnapshots();
  });
  if (mounted) {
    if (mount_point == "/data") {
      if (!PartitionManager.PrepareDataForDe(true)) return false;
      TWPartition* data = PartitionManager.FindPartitionByPath(mount_point);
      if (data == nullptr) return false;
      // DE preparation does not unlock a user's CE storage.
      if (data->is_fbe_ && TWPartition::DataIsLocked()) return true;
    } else if (!PartitionManager.MountByPath(mount_point, true)) {
      return false;
    }
    PartitionManager.AddMtpStorage(mount_point);
    return true;
  }
  return PartitionManager.UnmountByPath(mount_point, true) != 0;
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
  TWPartition* sys = PartitionManager.FindPartitionByPath("/system");

  operation_start("Check Partition Lifetime Writes");
  if (sys) {
    if (sys->CheckLifetimeWrites() != 0)
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
  PartitionManager.TryGetPartitionList("storage", &list);
  sort_otg_partitions(list);

  std::vector<storage_device> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ is_otg_path(entry.mount_point) ? otg_name(entry.mount_point, list) :
                       entry.display_name, entry.mount_point, entry.selected });
  return result;
}

// GUIPartitionList::NotifySelect for the "storage" list, which then sets
// tw_storage_path; that is what recomputes the backup folder, the display name
// and the free size.
bool twrp_mount_backend::select_storage(const std::string& path) {
  // Keep the target alive across lookup, mount and size refresh.
  twrp_operation_executor::guard transaction(twrp_operation_executor::instance().mutex());
  const auto publish = android::base::make_scope_guard([] {
    PartitionManager.RefreshPartitionListSnapshots();
  });
  TWPartition* Part = PartitionManager.FindPartitionByPath(path);
  if (Part == nullptr) {
    LOGERR("Unable to locate partition for '%s'\n", path.c_str());
    return false;
  }
  bool update_size = !Part->IsMounted() && Part->removable_;
  if (!Part->Mount(true)) return false;
  if (update_size && !Part->UpdateSize(true)) return false;
  DataManager::SetValue("tw_storage_path", path);
  return true;
}

std::string twrp_mount_backend::storage_name() {
  std::string value;
  DataManager::GetValue("tw_storage_display_name", value);
  if (!is_otg_path(DataManager::GetCurrentStoragePath())) return value;
  std::vector<PartitionList> list;
  PartitionManager.TryGetPartitionList("mount", &list);
  return otg_name(DataManager::GetCurrentStoragePath(), list);
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
    if (PartitionManager.EnableMtp())
      op_status = 0;  // success
    else
      op_status = 1;  // fail
  } else {
    operation_start("Stop MTP");
    if (PartitionManager.DisableMtp())
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
    if (!has_usb_storage() || !PartitionManager.UsbStorageEnable()) {
      DataManager::SetValue(TW_ACTION_BUSY, 0);
      return false;
    }
    // The partition manager requests the switch; init acknowledges it.
    if (!android::base::WaitForProperty("sys.usb.state", "mass_storage,adb",
                                        std::chrono::milliseconds(1500))) {
      PartitionManager.UsbStorageDisable();
      DataManager::SetValue(TW_ACTION_BUSY, 0);
      return false;
    }
  } else {
    if (!PartitionManager.UsbStorageDisable()) return false;
    DataManager::SetValue(TW_ACTION_BUSY, 0);
  }
  usb_storage_on_ = enabled;
  return true;
}

}  // namespace gui2_backend
