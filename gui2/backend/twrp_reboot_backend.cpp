#include "twrp_reboot_backend.h"

#include <sys/mount.h>
#include <unistd.h>

#include <cstdlib>

#include <android-base/properties.h>

#include "data.hpp"
#include "twcommon.h"
#include "twrp_operation.h"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "variables.h"

namespace gui2_backend {

twrp_reboot_backend::twrp_reboot_backend() = default;

// The conditions on the legacy reboot page's buttons.
const reboot_capabilities& twrp_reboot_backend::capabilities() const {
  const auto flag = [](const char* name) { return DataManager::GetIntValue(name) != 0; };
  capabilities_.system = flag(TW_REBOOT_SYSTEM);
  capabilities_.power_off = flag(TW_REBOOT_POWEROFF);
  capabilities_.recovery = flag(TW_REBOOT_RECOVERY);
  capabilities_.fastboot = flag(TW_FASTBOOT_MODE);
  capabilities_.bootloader = flag(TW_REBOOT_BOOTLOADER);
  capabilities_.download = flag(TW_DOWNLOAD_MODE);
  capabilities_.edl = flag(TW_EDL_MODE);
  capabilities_.boot_slots = flag("tw_has_boot_slots");
  return capabilities_;
}

std::string twrp_reboot_backend::active_slot() const {
  if (!capabilities().boot_slots) return {};
  return PartitionManager.Get_Active_Slot_Display();
}

// GUIAction::setbootslot
bool twrp_reboot_backend::set_active_slot(boot_slot slot) {
  const std::string arg = slot == boot_slot::A ? "A" : "B";
  operation_start("Set Boot Slot");
  if (PartitionManager.Find_Partition_By_Path("/vendor")) {
    if (!PartitionManager.UnMount_By_Path("/vendor", false)) {
      // PartitionManager failed to unmount /vendor, this should not happen,
      // but in case it does, do a lazy unmount
      LOGINFO("WARNING: vendor partition could not be unmounted normally!\n");
      PartitionManager.UnMount_By_Path("/vendor", false, MNT_DETACH);
    }
  }
  PartitionManager.Set_Active_Slot(arg);
  operation_end(0);
  return true;
}

bool twrp_reboot_backend::is_supported(reboot_target target) const {
  switch (target) {
    case reboot_target::SYSTEM:
      return capabilities().system;
    case reboot_target::POWER_OFF:
      return capabilities().power_off;
    case reboot_target::RECOVERY:
      return capabilities().recovery;
    case reboot_target::FASTBOOT:
      return capabilities().fastboot;
    case reboot_target::BOOTLOADER:
      return capabilities().bootloader;
    case reboot_target::DOWNLOAD:
      return capabilities().download;
    case reboot_target::EDL:
      return capabilities().edl;
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

// GUIAction::reboot
bool twrp_reboot_backend::request_reboot(reboot_target target) {
  const char* argument = reboot_argument(target);
  if (argument == nullptr || !is_supported(target)) return false;

  sync();
  return DataManager::SetValue("tw_reboot_arg", argument) == 0 &&
         DataManager::SetValue("tw_gui_done", 1) == 0;
}

// The fastboot page's switch, on tw_enable_fastboot.
bool twrp_reboot_backend::usb_fastboot() const {
  return DataManager::GetIntValue("tw_enable_fastboot") != 0;
}

// GUIAction::enableadb / enablefastboot, then the page's set.
void twrp_reboot_backend::set_usb_fastboot(bool fastboot) {
  android::base::SetProperty("sys.usb.config", "none");
  android::base::SetProperty("sys.usb.config", fastboot ? "fastboot" : "adb");
  DataManager::SetValue("tw_enable_fastboot", fastboot ? 1 : 0);
}

// The rebootcheck page's comparison.
bool twrp_reboot_backend::os_installed() const {
  const auto value = [](const char* name) {
    return std::strtoull(DataManager::GetStrValue(name).c_str(), nullptr, 10);
  };
  return value(TW_BACKUP_SYSTEM_SIZE) >= value(TW_MIN_SYSTEM_VAR);
}

}  // namespace gui2_backend
