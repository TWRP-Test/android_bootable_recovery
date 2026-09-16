#include "twrp_reboot_backend.h"

#include <sys/mount.h>
#include <unistd.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"

namespace gui2_backend {

twrp_reboot_backend::twrp_reboot_backend()
    : capabilities_{
        true, true,
#ifndef TW_NO_REBOOT_RECOVERY
        true,
#else
        false,
#endif
#if defined(TW_INCLUDE_FASTBOOTD) || defined(PRODUCT_USE_DYNAMIC_PARTITIONS)
        true,
#else
        false,
#endif
#ifndef TW_NO_REBOOT_BOOTLOADER
        true,
#else
        false,
#endif
#ifdef TW_HAS_DOWNLOAD_MODE
        true,
#else
        false,
#endif
#ifdef TW_HAS_EDL_MODE
        true,
#else
        false,
#endif
#ifdef AB_OTA_UPDATER
        true,
#else
        false,
#endif
      } {
#ifdef AB_OTA_UPDATER
  const std::string slot = PartitionManager.Get_Active_Slot_Display();
  capabilities_.boot_slots = slot == "A" || slot == "B";
#else
  capabilities_.boot_slots = false;
#endif
}

const reboot_capabilities& twrp_reboot_backend::capabilities() const {
  return capabilities_;
}

std::string twrp_reboot_backend::active_slot() const {
  if (!capabilities_.boot_slots) return {};
  return PartitionManager.Get_Active_Slot_Display();
}

bool twrp_reboot_backend::set_active_slot(boot_slot slot) {
  if (!capabilities_.boot_slots) return false;

  const std::string requested = slot == boot_slot::A ? "A" : "B";
  if (active_slot() == requested) return true;

  if (PartitionManager.Find_Partition_By_Path("/vendor") != nullptr &&
      !PartitionManager.UnMount_By_Path("/vendor", false)) {
    PartitionManager.UnMount_By_Path("/vendor", false, MNT_DETACH);
  }
  PartitionManager.Set_Active_Slot(requested);
  return active_slot() == requested;
}

bool twrp_reboot_backend::is_supported(reboot_target target) const {
  switch (target) {
    case reboot_target::SYSTEM:
      return capabilities_.system;
    case reboot_target::POWER_OFF:
      return capabilities_.power_off;
    case reboot_target::RECOVERY:
      return capabilities_.recovery;
    case reboot_target::FASTBOOT:
      return capabilities_.fastboot;
    case reboot_target::BOOTLOADER:
      return capabilities_.bootloader;
    case reboot_target::DOWNLOAD:
      return capabilities_.download;
    case reboot_target::EDL:
      return capabilities_.edl;
  }
  return false;
}

const char* twrp_reboot_backend::reboot_argument(reboot_target target) const {
  switch (target) {
    case reboot_target::SYSTEM:
      return "system";
    case reboot_target::POWER_OFF:
      return "poweroff";
    case reboot_target::RECOVERY:
      return "recovery";
    case reboot_target::FASTBOOT:
      return "fastboot";
    case reboot_target::BOOTLOADER:
      return "bootloader";
    case reboot_target::DOWNLOAD:
      return "download";
    case reboot_target::EDL:
      return "edl";
  }
  return nullptr;
}

bool twrp_reboot_backend::request_reboot(reboot_target target) {
  const char* argument = reboot_argument(target);
  if (argument == nullptr || !is_supported(target)) return false;

  sync();
  return DataManager::SetValue("tw_reboot_arg", argument) == 0 &&
         DataManager::SetValue("tw_gui_done", 1) == 0;
}

}  // namespace gui2_backend
