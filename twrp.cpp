/*
	Copyright 2012-2020 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <cutils/properties.h>

extern "C" {
#include "gui/gui.h"
}

#include "data.hpp"
#include "openrecoveryscript.hpp"
#include "partitions.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "twrpAdbBuFifo.hpp"
#include "twrp_functions.hpp"
#include "variables.h"
#include "gui/gui.hpp"
#include "gui/objects.hpp"
#include "gui/pages.hpp"
#include "gui/twmsg.h"
#include "recovery_utils/battery_utils.h"
#include "startup/startup_args.hpp"

#ifdef TW_INCLUDE_CRYPTO
#include "FsCrypt.h"
#include "Decrypt.h"
#endif

namespace fs = std::filesystem;

TWPartitionManager PartitionManager;
int Log_Offset;
bool datamedia;

static void DecryptPage(const bool skip_decryption, const bool data_media) {
  // Offer to decrypt if the device is encrypted
  if (DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0) {
    if (skip_decryption) {
      LOGINFO("Skipping decryption\n");
      PartitionManager.Update_System_Details(true);
    } else if (DataManager::GetIntValue(TW_CRYPTO_PWTYPE) != 0) {
      LOGINFO("Is encrypted, do decrypt page first\n");
      if (DataManager::GetIntValue(TW_IS_FBE)) DataManager::SetValue("tw_crypto_user_id", "0");
      if (gui_startPage("decrypt", 1, 1) != 0) {
        LOGERR("Failed to start decrypt GUI page.\n");
      }
    }
  } else if (data_media) {
    PartitionManager.Update_System_Details(true);
    if (tw_get_default_metadata(DataManager::GetCurrentStoragePath().c_str()) != 0) {
      LOGINFO("Failed to get default contexts and file mode for storage files.\n");
    } else {
      LOGINFO("Got default contexts and file mode for storage files.\n");
    }
  }
}

static void ProcessFastbootdMode() {
  LOGINFO("starting fastboot\n");

  if (android::base::GetBoolProperty("ro.boot.dynamic_partitions", false)) {
    PartitionManager.Unmap_Super_Devices();
  }

  gui_msg(Msg("fastboot_console_msg=Entered Fastbootd mode..."));
  // Check for and run startup script if script exists
  TWFunc::CheckAndRunScript("/system/bin/runatboot.sh", "boot");
  TWFunc::CheckAndRunScript("/system/bin/postfastboot.sh", "fastboot");
  if (gui_startPage("fastboot", 1, 1) != 0) {
    LOGERR("Failed to start fastbootd page.\n");
  }
}

static void ProcessRecoveryMode(twrpAdbBuFifo* adb_bu_fifo, const bool skip_decryption) {
  const int crash_counter = std::stoi(android::base::GetProperty("twrp.crash_counter", "-1")) + 1;
  android::base::SetProperty("twrp.crash_counter", std::to_string(crash_counter));

  if (crash_counter == 0) {
    property_list([](const char* key, const char* value, void*) {
      printf("%s=%s\n", key, value);
    }, nullptr);
    printf("\n");
  } else {
    printf("twrp.crash_counter=%d\n", crash_counter);
  }

  // We are doing this here to allow super partition to be set up prior to overriding properties
#if defined(TW_INCLUDE_LIBRESETPROP)
  for (const std::string& prop : std::initializer_list<std::string>{
    "ro.build.date.utc", "ro.bootimage.build.date.utc",
    "ro.vendor.build.date.utc", "ro.system.build.date.utc",
    "ro.system_ext.build.date.utc", "ro.product.build.date.utc",
    "ro.odm.build.date.utc",
  }) {
    TWFunc::OverrideProperty(prop, "0");
    LOGINFO("Overriding %s with value: \"0\"\n", prop.c_str());
  }
#if defined(TW_OVERRIDE_SYSTEM_PROPS)
  stringstream override_props(TW_OVERRIDE_SYSTEM_PROPS);
  string current_prop;

  std::vector<std::string> partition_list;
  partition_list.emplace_back(PartitionManager.Get_Android_Root_Path());
#ifdef TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS
  auto additional_partitions = TWFunc::SplitString(TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS, " ");
  partition_list.reserve(partition_list.size() + additional_partitions.size());
  for (auto& part : additional_partitions) partition_list.push_back(std::move(part));
#endif
  std::vector<std::string> build_prop_list = { "build.prop" };
#ifdef TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS
  auto additional_build_props = TWFunc::SplitString(TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS, ";");
  build_prop_list.reserve(build_prop_list.size() + additional_build_props.size());
  for (auto& build_prop : additional_build_props) build_prop_list.push_back(std::move(build_prop));
#endif
  while (getline(override_props, current_prop, ';')) {
    std::string other_prop;
    if (const auto eq_pos = current_prop.find('='); eq_pos != std::string::npos) {
      other_prop = current_prop.substr(eq_pos + 1);
      current_prop = current_prop.substr(0, eq_pos);
    } else {
      other_prop = current_prop;
    }
    other_prop = android::base::Trim(other_prop);
    current_prop = android::base::Trim(current_prop);

    for (const auto& partition_mount_point : partition_list) {
      for (const auto& prop_file : build_prop_list) {
        std::string sys_val = TWFunc::GetPropertyFromPartition(other_prop, PartitionManager,
                                                          partition_mount_point.c_str(), prop_file);
        if (!sys_val.empty()) {
          if (partition_mount_point == "/system_root") {
            LOGINFO("Overriding %s with value: \"%s\" from property %s in /system/%s\n",
                    current_prop.c_str(), sys_val.c_str(), other_prop.c_str(),
                    prop_file.c_str());
          } else {
            LOGINFO("Overriding %s with value: \"%s\" from property %s in /%s/%s\n",
                    current_prop.c_str(), sys_val.c_str(), other_prop.c_str(),
                    partition_mount_point.c_str(), prop_file.c_str());
          }
          int error = TWFunc::OverrideProperty(current_prop, sys_val);
          if (error) {
            LOGERR("Failed overriding property %s, error_code: %d\n", current_prop.c_str(), error);
          }
          if (partition_mount_point == partition_list.back()) {
            PartitionManager.UnMount_By_Path(partition_mount_point, false);
          }
          goto exit;
        } else {
          if (partition_mount_point == "/system_root") {
            LOGINFO("Unable to override property %s: property not found in /system/%s\n",
                    current_prop.c_str(), prop_file.c_str());
          } else {
            LOGINFO("Unable to override property %s: property not found in /%s/%s\n",
                    current_prop.c_str(), partition_mount_point.c_str(), prop_file.c_str());
          }
        }
      }
      PartitionManager.UnMount_By_Path(partition_mount_point, false);
    }
  exit:
    continue;
  }
#endif // defined(TW_OVERRIDE_SYSTEM_PROPS)
#endif // defined(TW_INCLUDE_LIBRESETPROP)

  // Check for and run startup script if script exists
  TWFunc::CheckAndRunScript("/system/bin/runatboot.sh", "boot");
  TWFunc::CheckAndRunScript("/system/bin/postrecoveryboot.sh", "recovery");

#ifdef TW_INCLUDE_CRYPTO
  android::keystore::syncKeystoreDb();
#endif
  DecryptPage(skip_decryption, datamedia);

  // Check for and load custom theme if present
  TWFunc::CheckSelinuxSupport();
  gui_loadCustomResources();
  PartitionManager.Output_Partition_Logging();

  // Fixup the RTC clock on devices which require it
  if (crash_counter == 0) TWFunc::FixupTimeOnBoot();

  DataManager::LoadTWRPFolderInfo();
  //DataManager::ReadSettingsFile();

  // Run any outstanding OpenRecoveryScript
  std::string cache_dir = TWFunc::GetLogDir();
  if (cache_dir == DATA_LOGS_DIR) cache_dir = "/data/cache";
  if (const std::string ors_file = cache_dir + "/recovery/openrecoveryscript";
    (DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0 || skip_decryption) &&
    (TWFunc::IsPathExists(SCRIPT_FILE_TMP) || TWFunc::IsPathExists(ors_file))) {
    OpenRecoveryScript::Run_OpenRecoveryScript();
  }

#ifdef TW_HAS_MTP
  const std::string mtp_crash_check = android::base::GetProperty("mtp.crash_check", "0");
  const bool try_mtp = DataManager::GetIntValue("tw_mtp_enabled")
      && mtp_crash_check == "0" && !crash_counter
      && (!DataManager::GetIntValue(TW_IS_ENCRYPTED) ||
          DataManager::GetIntValue(TW_IS_DECRYPTED));
  if (try_mtp) {
    android::base::SetProperty("mtp.crash_check", "1");
    LOGINFO("Starting MTP\n");
    if (!PartitionManager.Enable_MTP()) PartitionManager.Disable_MTP();
    else gui_msg("mtp_enabled=MTP Enabled");
    android::base::SetProperty("mtp.crash_check", "0");
  } else if (mtp_crash_check != "0") {
    gui_warn("mtp_crash=MTP Crashed, not starting MTP on boot.");
    DataManager::SetValue("tw_mtp_enabled", 0);
    PartitionManager.Disable_MTP();
  } else if (crash_counter == 1) {
    LOGINFO("TWRP crashed; disabling MTP as a precaution.\n");
    PartitionManager.Disable_MTP();
  }
#endif

  // Check if system has never been changed
  TWPartition* sys = PartitionManager.Find_Partition_By_Path(
      PartitionManager.Get_Android_Root_Path());
  TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
  if (sys) {
    if (sys->Get_Super_Status()) {
#ifdef TW_INCLUDE_CRYPTO
      std::string recovery_log_dir = std::string(DATA_LOGS_DIR) + "/recovery";
      if (!TWFunc::IsPathExists(recovery_log_dir)) {
        if (!PartitionManager.Recreate_Logs_Dir())
          LOGERR("Unable to create log directory for TWRP\n");
      }
#endif
    } else {
      if (const int mount_system_ro = DataManager::GetIntValue("tw_mount_system_ro");
        (mount_system_ro == 0 && sys->Check_Lifetime_Writes() == 0) || mount_system_ro == 2) {
        if (DataManager::GetIntValue("tw_never_show_system_ro_page") == 0) {
          DataManager::SetValue("tw_back", "main");
          if (gui_startPage("system_readonly", 1, 1) != 0) {
            LOGERR("Failed to start system_readonly GUI page.\n");
          }
        } else if (mount_system_ro == 0) {
          sys->Change_Mount_Read_Only(false);
          if (ven) ven->Change_Mount_Read_Only(false);
        }
      } else if (mount_system_ro != 1) {
        sys->Change_Mount_Read_Only(false);
        if (ven) ven->Change_Mount_Read_Only(false);
      }
    }
  }

  TWFunc::UpdateLogFile();

  adb_bu_fifo->threadAdbBuFifo();

  // Disable flashing of stock recovery
  TWFunc::DisableStockRecoveryReplace();
}

static void Reboot() {
  gui_msg(Msg("rebooting=Rebooting..."));
  TWFunc::UpdateLogFile();
  std::string reboot_arg;
  DataManager::GetValue("tw_reboot_arg", reboot_arg);
  if (reboot_arg == "recovery") TWFunc::TwReboot(RECOVERY);
  else if (reboot_arg == "poweroff") TWFunc::TwReboot(POWER_OFF);
  else if (reboot_arg == "bootloader") TWFunc::TwReboot(BOOTLOADER);
  else if (reboot_arg == "download") TWFunc::TwReboot(DOWNLOAD);
  else if (reboot_arg == "edl") TWFunc::TwReboot(EDL);
  else if (reboot_arg == "fastboot") TWFunc::TwReboot(FSATBOOTD);
  else TWFunc::TwReboot(SYSTEM);
}

static constexpr auto kBatteryPollInterval = std::chrono::milliseconds(250);

#ifdef TW_USE_LEGACY_BATTERY_SERVICES
static constexpr int kBatteryHiddenSentinelLow = 0;
static constexpr int kBatteryHiddenSentinelHigh = 101;

static BatteryInfo ReadLegacyBattery() {
#ifdef TW_CUSTOM_BATTERY_PATH
  const auto battery_path = fs::path(EXPAND(TW_CUSTOM_BATTERY_PATH));
#else
  const auto battery_path = fs::path("/sys/class/power_supply/battery");
#endif

  const std::string cap_path = battery_path / "capacity";
  const std::string st_path = battery_path / "status";

  BatteryInfo battery_info{};

  // Sysfs capacity: single integer (0–100), newline-terminated
  if (std::ifstream f(cap_path); f) {
    if (std::string line; std::getline(f, line)) {
      const int v = std::stoi(line);
      battery_info.capacity = v > 100
                                ? kBatteryHiddenSentinelHigh
                                : v < 0 ? kBatteryHiddenSentinelLow : v;
    }
  }

  // Sysfs status: "Charging", "Discharging", "Full", etc.
  if (std::ifstream f(st_path); f) {
    std::string line;
    battery_info.charging = std::getline(f, line) && line == "Charging";
  }

  return battery_info;
}
#endif

static void MonitorBattery() {
  for (;;) {
#ifdef TW_USE_LEGACY_BATTERY_SERVICES
    const auto [charging, capacity] = ReadLegacyBattery();
#else
    const auto [charging, capacity] = GetBatteryInfo();
#endif
    DataManager::SetValue("tw_battery", std::format("{}%{}", capacity, charging ? "+" : ""));
    std::this_thread::sleep_for(kBatteryPollInterval);
  }
}

int main(int argc, char* argv[]) {
  // Recovery needs to install world-readable files, so clear umask
  // set by init
  umask(0);
  Log_Offset = 0;

  // Set up temporary log file (/tmp/recovery.log)
  freopen(TMP_LOG_FILE, "a", stdout);
  setbuf(stdout, nullptr);
  freopen(TMP_LOG_FILE, "a", stderr);
  setbuf(stderr, nullptr);

  signal(SIGPIPE, SIG_IGN);

  // Handle ADB sideload
  if (argc == 3 && strcmp(argv[1], "--adbd") == 0) {
    android::base::SetProperty("ctl.stop", "adbd");
    return 0;
  }

#ifdef RECOVERY_SDCARD_ON_DATA
  datamedia = true;
#endif

  android::base::SetProperty("ro.twrp.boot", "1");
  android::base::SetProperty("ro.twrp.version", TWFunc::GetTwrpVersion());

#ifdef TARGET_OTA_ASSERT_DEVICE
  android::base::SetProperty("ro.twrp.target.devices", TARGET_OTA_ASSERT_DEVICE);
#endif

  const time_t startup_time = time(nullptr);
  printf("Starting TWRP %s-%s on %s (pid %d)\n", TWFunc::GetTwrpVersion().c_str(), TW_GIT_REVISION,
         ctime(&startup_time), getpid());

  // Load default values to set DataManager constants and handle ifdefs
  DataManager::SetDefaultValues();

  StartupArgs startup;
  startup.Parse(&argc, &argv);

  android::base::SetProperty(TW_FASTBOOT_MODE_PROP, startup.GetFastbootMode() ? "1" : "0");
  printf("=> Linking mtab\n");
  symlink("/proc/mounts", "/etc/mtab");
  std::string fstab_filename = "/etc/twrp.fstab";
  if (!TWFunc::IsPathExists(fstab_filename)) {
    fstab_filename = "/etc/recovery.fstab";
  }
  printf("=> Processing %s\n", fstab_filename.c_str());
  if (!PartitionManager.Process_Fstab(fstab_filename, true, !startup.GetFastbootMode())) {
    LOGERR("Failing out of recovery due to problem with fstab.\n");
    return -1;
  }

#ifdef TW_LOAD_VENDOR_MODULES
  if (startup.GetFastbootMode()) {
    std::vector<std::string> prepare_parts = {
      "/system_root",
      "/vendor",
      "/vendor_dlkm",
      "/odm"
    };
    for (auto& prepare_part : prepare_parts) {
      TWPartition* part = PartitionManager.Find_Partition_By_Path(prepare_part);
      if (part) PartitionManager.Prepare_Super_Volume(part);
    }
  }
#endif

  printf("Starting the UI...\n");
  gui_init();

  if (!startup.GetFastbootMode()) PartitionManager.Setup_Fstab_Partitions(true);

  // Load up all the resources
  gui_loadResources();

  DataManager::ReadSettingsFile();
  PageManager::LoadLanguage(DataManager::GetStrValue("tw_language"));

  // Create a thread for battery monitoring
  std::thread(MonitorBattery).detach();

  auto* adb_bu_fifo = new twrpAdbBuFifo();
  TWFunc::ClearBootloaderMessage();

  if (startup.GetFastbootMode()) {
    ProcessFastbootdMode();
    delete adb_bu_fifo;
    TWFunc::UpdateIntentFile(startup.GetIntent());
    Reboot();
    return 0;
  }
  ProcessRecoveryMode(adb_bu_fifo, startup.ShouldSkipDecryption());

  GUIConsole::Translate_Now();

  // Launch the main GUI
  gui_start();
  delete adb_bu_fifo;
  TWFunc::UpdateIntentFile(startup.GetIntent());
  Reboot();

  return 0;
}