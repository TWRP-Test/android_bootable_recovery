#include "twrp_mount_backend.h"

#include <cstdio>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "variables.h"

namespace gui2_backend {

std::vector<mount_target> twrp_mount_backend::targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("mount", &list);

  std::vector<mount_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.Display_Name, entry.Mount_Point, entry.selected });
  return result;
}

bool twrp_mount_backend::set_mounted(const std::string& mount_point, bool mounted) {
  if (mounted) {
    if (PartitionManager.Mount_By_Path(mount_point, true) == 0) return false;
    PartitionManager.Add_MTP_Storage(mount_point);
    return true;
  }
  return PartitionManager.UnMount_By_Path(mount_point, true) != 0;
}

bool twrp_mount_backend::system_writable() {
  return DataManager::GetIntValue("tw_mount_system_ro") == 0;
}

// Mirrors the legacy toggle: system has to come down before the flag changes,
// and vendor follows it so the two never disagree.
bool twrp_mount_backend::set_system_writable(bool writable) {
  const std::string root = PartitionManager.Get_Android_Root_Path();
  const bool remount_system = PartitionManager.Is_Mounted_By_Path(root) != 0;
  const bool remount_vendor = PartitionManager.Is_Mounted_By_Path("/vendor") != 0;

  if (PartitionManager.UnMount_By_Path(root, true) == 0) return false;

  TWPartition* system = PartitionManager.Find_Partition_By_Path(root);
  if (system == nullptr) return false;
  DataManager::SetValue("tw_mount_system_ro", writable ? 0 : 1);
  system->Change_Mount_Read_Only(!writable);
  if (remount_system) system->Mount(true);

  TWPartition* vendor = PartitionManager.Find_Partition_By_Path("/vendor");
  if (vendor != nullptr) {
    vendor->Change_Mount_Read_Only(!writable);
    if (remount_vendor) vendor->Mount(true);
  }
  return true;
}

std::vector<storage_device> twrp_mount_backend::storages() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("storage", &list);

  std::vector<storage_device> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.Display_Name, entry.Mount_Point, entry.selected });
  return result;
}

// Same order as the legacy list: the storage has to come up first, and the
// path is only recorded once it did. Setting it is what recomputes the backup
// folder, the display name and the free size.
bool twrp_mount_backend::select_storage(const std::string& path) {
  TWPartition* partition = PartitionManager.Find_Partition_By_Path(path);
  if (partition == nullptr) return false;
  // The legacy list checks Removable here, but that member is private to its
  // friends. "Was not mounted" is the same signal in practice: a card that just
  // came up has no size recorded yet.
  const bool update_size = !partition->Is_Mounted();
  if (!partition->Mount(true)) return false;
  if (update_size && !partition->Update_Size(true)) return false;
  return DataManager::SetValue("tw_storage_path", path) == 0;
}

std::string twrp_mount_backend::storage_name() {
  std::string value;
  DataManager::GetValue("tw_storage_display_name", value);
  return value;
}

std::string twrp_mount_backend::storage_free() {
  std::string value;
  DataManager::GetValue("tw_storage_free_size", value);
  return value;
}

bool twrp_mount_backend::mtp_enabled() {
  return DataManager::GetIntValue("tw_mtp_enabled") != 0;
}

bool twrp_mount_backend::set_mtp_enabled(bool enabled) {
  return enabled ? PartitionManager.Enable_MTP() : PartitionManager.Disable_MTP();
}

bool twrp_mount_backend::has_usb_storage() {
  char lun_file[255];
  snprintf(lun_file, sizeof(lun_file), CUSTOM_LUN_FILE, 0);
  return TWFunc::Path_Exists(lun_file);
}

bool twrp_mount_backend::usb_storage_enabled() {
  return usb_storage_on_;
}

bool twrp_mount_backend::set_usb_storage_enabled(bool enabled) {
  const bool ok = enabled ? PartitionManager.usb_storage_enable() != 0
                          : PartitionManager.usb_storage_disable() != 0;
  if (ok) usb_storage_on_ = enabled;
  return ok;
}

}  // namespace gui2_backend
