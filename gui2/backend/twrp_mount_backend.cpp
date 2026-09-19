#include "twrp_mount_backend.h"

#include "data.hpp"
#include "partitions.hpp"
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

}  // namespace gui2_backend
