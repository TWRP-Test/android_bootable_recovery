/*
	Copyright 2012 to 2021 TeamWin
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

#include <pthread.h>
#include <unistd.h>

#include <charconv>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>

#include <android-base/file.h>
#include <android-base/parsedouble.h>
#include <android-base/stringprintf.h>
#include <cutils/properties.h>
#include <fstab/fstab.h>

#include "data.hpp"
#ifndef TW_NO_SCREEN_TIMEOUT
#include "gui/blanktimer.hpp"
#endif

#include "gui/gui.hpp"
#include "gui/pages.h"
#include "infomanager.hpp"
#include "variables.h"
#include "partitions.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "twrpminui/minui.h"
#include "twrp-functions.hpp"
#include "unit_conversion.hpp"

#define FILE_VERSION 0x00010010 // Do not set to 0


std::string DataManager::kBackingFile;
bool DataManager::initialized_ = false;
// Data that that is not constant and will be saved to the settings file
InfoManager DataManager::persist_;
// Data that is not constant and will not be saved to settings file
InfoManager DataManager::data_;
// Data that is constant and will not be saved to settings file
InfoManager DataManager::consts_;

extern bool datamedia;

#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
pthread_mutex_t DataManager::values_lock_ = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
pthread_mutex_t DataManager::values_lock_ = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

namespace fs = std::filesystem;

void DataManager::SetDeviceId() {
  std::string serial_no;

#ifdef TW_USE_SERIALNO_PROPERTY_FOR_DEVICE_ID
  // Get serial number from system property
  serial_no = android::base::GetProperty("ro.serialno", "");
#else
  // Get serial number from bootconfig
  android::fs_mgr::GetBootconfig("androidboot.serialno", &serial_no);
#endif
  consts_["device_id"] = serial_no;
}

int DataManager::ResetDefaults() {
  pthread_mutex_lock(&values_lock_);
  persist_.Clear();
  data_.Clear();
  consts_.Clear();
  pthread_mutex_unlock(&values_lock_);

  SetDefaultValues();
  return 0;
}

int DataManager::LoadValues(const std::string& filename) {
  std::string dev_id;

  if (!initialized_) SetDefaultValues();

  GetValue("device_id", dev_id);
  // Save off the backing file for set operations
  kBackingFile = filename;
  persist_.SetFile(filename);
  persist_.SetFileVersion(FILE_VERSION);

  // Read in the file, if possible
  pthread_mutex_lock(&values_lock_);
  persist_.LoadValues();

#ifndef TW_NO_SCREEN_TIMEOUT
  if (const std::optional<int> timeout = persist_["tw_screen_timeout_secs"]) {
    blankTimer.setTime(timeout.value());
  }
#endif

  pthread_mutex_unlock(&values_lock_);
  const std::string current = GetCurrentStoragePath();
  TWPartition* Part = PartitionManager.Find_Partition_By_Path(current);
  if (!Part) Part = PartitionManager.Get_Default_Storage_Partition();
  if (Part && current != Part->Storage_Path && Part->Mount(false)) {
    LOGINFO("LoadValues setting storage path to '%s'\n", Part->Storage_Path.c_str());
    SetValue("tw_storage_path", Part->Storage_Path);
  } else {
    SetBackupFolder();
  }
  return 0;
}

int DataManager::Flush() {
  return SaveValues();
}

int DataManager::SaveValues() {
  if (kBackingFile.empty()) return -1;

  //string mount_path = GetSettingsStoragePath();
  //PartitionManager.Mount_By_Path(mount_path.c_str(), 1);

  //mPersist.SetFile(mBackingFile);
  persist_.SetFile(fs::path(TW_PERSIST_DIR) / TW_SETTINGS_FILE);
  persist_.SetFileVersion(FILE_VERSION);
  pthread_mutex_lock(&values_lock_);
  persist_.SaveValues();
  pthread_mutex_unlock(&values_lock_);

  tw_set_default_metadata(kBackingFile.c_str());
  LOGINFO("Saved settings file values to '%s'\n", kBackingFile.c_str());
  return 0;
}

int DataManager::GetValue(const std::string& key, std::string& value) {
  std::string local_key(key);

  if (!initialized_) SetDefaultValues();

  // Strip off leading and trailing '%' if provided
  if (local_key.length() > 2 && local_key[0] == '%' && local_key[local_key.length() - 1] == '%') {
    local_key.erase(0, 1);
    local_key.erase(local_key.length() - 1, 1);
  }

  // Handle magic values
  if (GetMagicValue(local_key, value) == 0) return 0;

  // Handle property
  if (local_key.length() > 9 && local_key.substr(0, 9) == "property.") {
    char property_value[PROPERTY_VALUE_MAX];
    property_get(local_key.substr(9).c_str(), property_value, "");
    value = property_value;
    return 0;
  }

  pthread_mutex_lock(&values_lock_);
  if (const std::optional<std::string> const_value = consts_[local_key]) {
    value = *const_value;
    goto exit;
  }
  if (const std::optional<std::string> persist_value = persist_[local_key]) {
    value = *persist_value;
    goto exit;
  }
  if (const std::optional<std::string> persist_value = data_[local_key]) {
    value = *persist_value;
    goto exit;
  }

  pthread_mutex_unlock(&values_lock_);
  return -1;

exit:
  pthread_mutex_unlock(&values_lock_);
  return 0;
}

int DataManager::GetValue(const std::string& key, int& value) {
  std::string data;

  if (GetValue(key, data) != 0) return -1;

  value = 0;
  std::from_chars(data.data(), data.data() + data.size(), value);
  return 0;
}

int DataManager::GetValue(const std::string& key, float& value) {
  std::string data;

  if (GetValue(key, data) != 0) return -1;

  value = 0;
  android::base::ParseFloat(data, &value);
  return 0;
}

int DataManager::GetValue(const std::string& key, uint64_t& value) {
  std::string data;

  if (GetValue(key, data) != 0) return -1;

  value = 0;
  std::from_chars(data.data(), data.data() + data.size(), value);
  return 0;
}

// This function will return an empty string if the value doesn't exist
std::string DataManager::GetStrValue(const std::string& key) {
  std::string retVal;

  GetValue(key, retVal);
  return retVal;
}

// This function will return 0 if the value doesn't exist
int DataManager::GetIntValue(const std::string& key) {
  std::string retVal;

  GetValue(key, retVal);
  int value = 0;
  std::from_chars(retVal.data(), retVal.data() + retVal.size(), value);
  return value;
}

int DataManager::SetValue(const std::string& key, const std::string& value,
                          const bool persist /* = false */) {
  if (!initialized_) SetDefaultValues();

  // Handle property
  if (key.length() > 9 && key.substr(0, 9) == "property.") {
    const int ret = property_set(key.substr(9).c_str(), value.c_str());
    if (ret)
      LOGERR("Error setting property '%s' to '%s'\n", key.substr(9).c_str(), value.c_str());
    return ret;
  }

  // Don't allow empty values or numerical starting values
  if (key.empty() || std::isdigit(key.front())) return -1;

  pthread_mutex_lock(&values_lock_);

  if (const std::optional<std::string> const_chk = consts_[key]) {
    pthread_mutex_unlock(&values_lock_);
    return -1;
  }

  if (persist) {
    persist_[key] = value;
  } else {
    if (const std::optional<std::string> persist_chk = persist_[key]) {
      persist_[key] = value;
    } else {
      data_[key] = value;
    }
  }

  pthread_mutex_unlock(&values_lock_);

#ifndef TW_NO_SCREEN_TIMEOUT
  if (key == "tw_screen_timeout_secs") {
    int timeout = 0;
    std::from_chars(value.data(), value.data() + value.size(), timeout);
    blankTimer.setTime(timeout);
  } else
#endif
    if (key == "tw_storage_path") {
      SetBackupFolder();
    }
  gui_notifyVarChange(key.c_str(), value.c_str());
  return 0;
}

int DataManager::SetValue(const std::string& key, const int value,
                          const bool persist /* = false */) {
  return SetValue(key, std::to_string(value), persist);
}

int DataManager::SetValue(const std::string& key, const float value,
                          const bool persist /* = false */) {
  return SetValue(key, std::format("{:.6g}", value), persist);
}

int DataManager::SetValue(const std::string& key, const uint64_t value,
                          const bool persist /* = false */) {
  return SetValue(key, std::to_string(value), persist);
}

// scoped=false (default): legacy absolute mode — claim the full bar, set the
// fraction, then release the scope. scoped=true: honor the active portion set by
// ShowProgress (former _SetProgress, used by the updater set_progress command).
int DataManager::SetProgress(float fraction, const bool scoped) {
  if (!scoped) {
    if (SetValue("ui_portion_size", 0) != 0) return -1;
    if (SetValue("ui_portion_start", 0) != 0) return -1;
    ShowProgress(1, 0);
  }

  float Portion_Start, Portion_Size;
  GetValue("ui_portion_size", Portion_Size);
  GetValue("ui_portion_start", Portion_Start);
  //LOGINFO("SetProgress(%.2lf): Portion_Size: %.2lf Portion_Start: %.2lf\n", Fraction, Portion_Size, Portion_Start);
  if (fraction < 0.0f) fraction = 0;
  if (fraction > 1.0f) fraction = 1;

  // res mirrors the former _SetProgress return: -1 if the ui_progress write
  // failed (ui_progress_portion left untouched), else (ui_progress_portion
  // write failed) ? 1 : 0.
  int res = 0;
  if (SetValue("ui_progress", (Portion_Start + Portion_Size * fraction) * 100.0f) != 0) {
    res = -1;
  } else {
    res = SetValue("ui_progress_portion", 0) != 0;
  }

  if (!scoped) {
    if (SetValue("ui_portion_size", 0) != 0) return -1;
    if (SetValue("ui_portion_start", 0) != 0) return -1;
  }
  return res;
}

int DataManager::ShowProgress(float portion, const float seconds) {
  float portion_start, portion_size;
  GetValue("ui_portion_size", portion_size);
  GetValue("ui_portion_start", portion_start);
  portion_start += portion_size;
  if (portion + portion_start > 1.0) portion = 1 - portion_start;
  //LOGINFO("ShowProgress(%.2lf, %.2lf): Portion_Start: %.2lf\n", Portion, Seconds, Portion_Start);
  if (SetValue("ui_portion_start", portion_start) != 0) return -1;
  if (SetValue("ui_portion_size", portion) != 0) return -1;
  if (SetValue("ui_progress", static_cast<float>(portion_start * 100.0)) != 0) return -1;
  if (seconds) {
    if (SetValue("ui_progress_portion", static_cast<float>(portion * 100.0 + portion_start)) !=
        0)
      return -1;
    if (SetValue("ui_progress_frames", seconds * 48) != 0) return -1;
  }
  return 0;
}

void DataManager::UpdateTimezoneEnvironment() {
  const std::string timezone_value = GetStrValue(TW_TIME_ZONE_VAR);
  setenv("TZ", timezone_value.c_str(), 1);
  tzset();
  android::base::SetProperty("persist.sys.timezone", timezone_value);
}

void DataManager::SetBackupFolder() {
  const auto storage = GetCurrentStoragePath();
  const TWPartition* partition = PartitionManager.Find_Partition_By_Path(storage);
  auto backup_path = fs::path(storage) / fs::path(TWFunc::Check_For_TwrpFolder()).relative_path() /
                     "BACKUPS";

  std::string dev_id;
  GetValue("device_id", dev_id);

  backup_path /= dev_id;
  LOGINFO("Backup folder set to '%s'\n", backup_path.c_str());
  SetValue(TW_BACKUPS_FOLDER_VAR, backup_path, 0);
  if (partition) {
    SetValue("tw_storage_display_name", partition->Storage_Name);
    SetValue("tw_storage_free_size", UnitConversion::FormatBytes(partition->Free));
    std::string zip_path, zip_root, storage_path;
    GetValue(TW_ZIP_LOCATION_VAR, zip_path);
    if (partition->Has_Data_Media && !partition->Symlink_Mount_Point.
        empty())
      storage_path = partition->Symlink_Mount_Point;
    else storage_path = partition->Storage_Path;
    if (zip_path.size() < storage_path.size()) {
      SetValue(TW_ZIP_LOCATION_VAR, storage_path);
    } else {
      zip_root = TWFunc::Get_Root_Path(zip_path);
      if (zip_root != storage_path) {
        LOGINFO("DataManager::SetBackupFolder zip path was %s changing to %s, %s\n",
                zip_path.c_str(), storage_path.c_str(), zip_root.c_str());
        SetValue(TW_ZIP_LOCATION_VAR, storage_path);
      }
    }
  } else {
    if (PartitionManager.Fstab_Processed() != 0) {
      LOGINFO("Storage partition '%s' not found\n", backup_path.c_str());
      gui_err("unable_locate_storage=Unable to locate storage device.");
    }
  }
}

// Recursively search root for a regular file named name, returning the first
// match (empty string if none). Symlinked directories are followed -- sysfs
// /sys/class/* entries are typically symlinks into /sys/devices -- with
// canonical-path de-dup to guard against sysfs symlink loops.
static std::string find_first_named_file(const std::string& name, const std::string& root) {
  std::error_code ec;
  std::unordered_set<fs::path> visited;
  constexpr auto opts = fs::directory_options::follow_directory_symlink
                        | fs::directory_options::skip_permission_denied;
  for (auto it = fs::recursive_directory_iterator(root, opts, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    const auto& entry = *it;
    if (entry.is_symlink()) {
      if (auto can = fs::canonical(entry.path(), ec); !ec && !visited.insert(can).second) {
        it.disable_recursion_pending(); // break a symlink loop
        continue;
      }
      ec.clear(); // reset after canonical(); dangling -> carry on
    }
    if (!entry.is_symlink() && entry.is_regular_file()
        && entry.path().filename() == name)
      return entry.path();
  }
  return "";
}

void DataManager::SetDefaultValues() {
  std::string str, path;

  consts_.SetConst();

  SetDeviceId();

  pthread_mutex_lock(&values_lock_);

  initialized_ = true;

  consts_["true"] = true;
  consts_["false"] = false;

  consts_[TW_VERSION_VAR] = TWFunc::Get_TWRP_Version_Str();

#ifndef TW_NO_HAPTICS
  persist_["tw_button_vibrate"] = 80;
  persist_["tw_keyboard_vibrate"] = 40;
  persist_["tw_action_vibrate"] = 160;
  consts_["tw_disable_haptics"] = false;
#else
  LOGINFO("TW_NO_HAPTICS := true\n");
  consts_["tw_disable_haptics"] = true;
#endif

#ifdef TW_INCLUDE_WIFI
  consts_["tw_disable_network"] = false;
#else
  LOGINFO("TW_INCLUDE_WIFI is not enabled\n");
  consts_["tw_disable_network"] = true;
#endif

  if (const TWPartition* store = PartitionManager.
      Get_Default_Storage_Partition())
    persist_["tw_storage_path"] = store->Storage_Path;
  else persist_["tw_storage_path"] = "/";

#ifdef TW_FORCE_CPUINFO_FOR_DEVICE_ID
  printf("TW_FORCE_CPUINFO_FOR_DEVICE_ID := true\n");
#endif

#ifdef BOARD_HAS_NO_REAL_SDCARD
  printf("BOARD_HAS_NO_REAL_SDCARD := true\n");
  consts_[TW_ALLOW_PARTITION_SDCARD] = false;
#else
  consts_[TW_ALLOW_PARTITION_SDCARD] = true;
#endif

  data_[TW_RECOVERY_FOLDER_VAR] = TW_DEFAULT_RECOVERY_FOLDER;

  const std::string current_storage_path = GetCurrentStoragePath();
  const std::optional<std::string> dev_id = consts_["device_id"];

  str = fs::path(GetCurrentStoragePath()) /
    fs::path(GetStrValue(TW_RECOVERY_FOLDER_VAR)).relative_path() / "BACKUPS" / *dev_id;
  persist_[TW_ZIP_LOCATION_VAR] = current_storage_path;
  data_[TW_BACKUPS_FOLDER_VAR] = str;

  consts_[TW_REBOOT_SYSTEM] = true;

#ifdef TW_NO_REBOOT_RECOVERY
  printf("TW_NO_REBOOT_RECOVERY := true\n");
  consts_[TW_REBOOT_RECOVERY] = false;
#else
  consts_[TW_REBOOT_RECOVERY] = true;
#endif

  consts_[TW_REBOOT_POWEROFF] = true;

#ifdef TW_NO_REBOOT_BOOTLOADER
  printf("TW_NO_REBOOT_BOOTLOADER := true\n");
  consts_[TW_REBOOT_BOOTLOADER] = false;
#else
  consts_[TW_REBOOT_BOOTLOADER] = true;
#endif

#ifdef RECOVERY_SDCARD_ON_DATA
  printf("RECOVERY_SDCARD_ON_DATA := true\n");
  consts_[TW_HAS_DATA_MEDIA] = true;
  datamedia = true;
#else
  data_[TW_HAS_DATA_MEDIA] = false;
#endif

#ifdef TW_NO_BATT_PERCENT
  printf("TW_NO_BATT_PERCENT := true\n");
  consts_[TW_NO_BATTERY_PERCENT] = true;
#else
  consts_[TW_NO_BATTERY_PERCENT] = false;
#endif

#ifdef TW_NO_CPU_TEMP
  printf("TW_NO_CPU_TEMP := true\n");
  consts_["tw_no_cpu_temp"] = true;
#else
#ifdef TW_CUSTOM_CPU_TEMP_PATH
  const std::string cpu_temp_file = EXPAND(TW_CUSTOM_CPU_TEMP_PATH);
#else
  const std::string cpu_temp_file = "/sys/class/thermal/thermal_zone0/temp";
#endif
  if (TWFunc::Path_Exists(cpu_temp_file)) {
    consts_["tw_no_cpu_temp"] = false;
  } else {
    LOGINFO("CPU temperature file '%s' not found, disabling CPU temp.\n", cpu_temp_file.c_str());
    consts_["tw_no_cpu_temp"] = true;
  }
#endif

#ifdef TW_CUSTOM_POWER_BUTTON
  printf("TW_POWER_BUTTON := %s\n", EXPAND(TW_CUSTOM_POWER_BUTTON));
  consts_[TW_POWER_BUTTON] = EXPAND(TW_CUSTOM_POWER_BUTTON);
#else
  consts_[TW_POWER_BUTTON] = false;
#endif

#ifdef TW_ALWAYS_RMRF
  printf("TW_ALWAYS_RMRF := true\n");
  consts_[TW_RM_RF_VAR] = true;
#endif

#ifdef TW_NEVER_UNMOUNT_SYSTEM
  printf("TW_NEVER_UNMOUNT_SYSTEM := true\n");
  consts_[TW_DONT_UNMOUNT_SYSTEM] = true;
#else
  consts_[TW_DONT_UNMOUNT_SYSTEM] = false;
#endif

#ifdef TW_NO_USB_STORAGE
  printf("TW_NO_USB_STORAGE := true\n");
  consts_[TW_HAS_USB_STORAGE] = false;
#else
  std::string lun_file_path = CUSTOM_LUN_FILE;
  if (lun_file_path.find('%') != std::string::npos) {
    lun_file_path = android::base::StringPrintf(CUSTOM_LUN_FILE, 0);
  }
  if (!TWFunc::Path_Exists(lun_file_path)) {
    LOGINFO("Lun file '%s' does not exist, USB storage mode disabled\n", lun_file_path.c_str());
    consts_[TW_HAS_USB_STORAGE] = false;
  } else {
    LOGINFO("Lun file '%s'\n", lun_file_path.c_str());
    data_[TW_HAS_USB_STORAGE] = true;
  }
#endif

#ifdef TW_HAS_DOWNLOAD_MODE
  printf("TW_HAS_DOWNLOAD_MODE := true\n");
  consts_[TW_DOWNLOAD_MODE] = true;
#endif

#ifdef TW_HAS_EDL_MODE
  printf("TW_HAS_EDL_MODE := true\n");
  consts_[TW_EDL_MODE] = true;
#endif

#ifdef TW_INCLUDE_FASTBOOTD
  printf("TW_INCLUDE_FASTBOOTD := true\n");
  consts_[TW_FASTBOOT_MODE] = true;
#endif

#ifdef PRODUCT_USE_DYNAMIC_PARTITIONS
  printf("PRODUCT_USE_DYNAMIC_PARTITIONS := true\n");
  consts_[TW_FASTBOOT_MODE] = true;
  consts_[TW_IS_SUPER] = true;
#else
  consts_[TW_IS_SUPER] = false;
#endif

#ifdef TW_INCLUDE_CRYPTO
  consts_[TW_HAS_CRYPTO] = true;
  printf("TW_INCLUDE_CRYPTO := true\n");
#endif

#ifdef TW_SDEXT_NO_EXT4
  printf("TW_SDEXT_NO_EXT4 := true\n");
  consts_[TW_SDEXT_DISABLE_EXT4] = true;
#else
  consts_[TW_SDEXT_DISABLE_EXT4] = false;
#endif

#ifdef TW_HAS_NO_BOOT_PARTITION
  persist_["tw_backup_list"] = "/system;/data;";
#else
#ifdef PRODUCT_USE_DYNAMIC_PARTITIONS
  persist_["tw_backup_list"] = "/data;";
#else
  persist_["tw_backup_list"] = "/system;/data;/boot;";
#endif
#endif

  consts_[TW_MIN_SYSTEM_VAR] = TW_MIN_SYSTEM_SIZE;
  data_[TW_BACKUP_NAME] = "(Auto Generate)";

  persist_[TW_INSTALL_REBOOT_VAR] = false;
  persist_[TW_SIGNED_ZIP_VERIFY_VAR] = false;
  persist_[TW_DISABLE_FREE_SPACE_VAR] = false;
  persist_[TW_FORCE_DIGEST_CHECK_VAR] = false;
  persist_[TW_USE_COMPRESSION_VAR] = false;
  persist_[TW_TIME_ZONE_VAR] = "CST6CDT,M3.2.0,M11.1.0";
  persist_[TW_GUI_SORT_ORDER] = true;
  persist_[TW_RM_RF_VAR] = false;
  persist_[TW_SKIP_DIGEST_CHECK_VAR] = false;
  persist_[TW_SKIP_DIGEST_CHECK_ZIP_VAR] = true;
  persist_[TW_SKIP_DIGEST_GENERATE_VAR] = false;
  persist_[TW_SDEXT_SIZE] = false;
  persist_[TW_SWAP_SIZE] = false;
  persist_[TW_SDPART_FILE_SYSTEM] = "ext3";
  persist_[TW_TIME_ZONE_GUISEL] = "CST6;CDT,M3.2.0,M11.1.0";
  persist_[TW_TIME_ZONE_GUIOFFSET] = false;
  persist_[TW_TIME_ZONE_GUIDST] = false;
  persist_[TW_AUTO_REFLASHTWRP_VAR] = false;

#ifdef TW_NO_FLASH_CURRENT_TWRP
  consts_["tw_no_flash_current_twrp"] = true;
#else
  consts_["tw_no_flash_current_twrp"] = false;
#endif

  persist_[TW_AUTO_DISABLE_AVB2_VAR] = false;
  data_[TW_ACTION_BUSY] = false;
  data_["tw_wipe_cache"] = false;
  data_["tw_wipe_dalvik"] = false;
  data_[TW_ZIP_INDEX] = false;
  data_[TW_ZIP_QUEUE_COUNT] = false;
  data_[TW_FILENAME] = "/sdcard";
  data_[TW_SIMULATE_ACTIONS] = false;
  data_[TW_SIMULATE_FAIL] = false;
  data_[TW_IS_ENCRYPTED] = false;
  data_[TW_IS_DECRYPTED] = false;
  data_[TW_CRYPTO_PASSWORD] = false;
  data_[TW_CRYPTO_PWTYPE] = false;
  // Set initial value so that recovery will not be confused when using unencrypted data or failed to decrypt data
  data_["tw_terminal_state"] = false;
  data_["tw_background_thread_running"] = false;
  data_[TW_RESTORE_FILE_DATE] = false;
  persist_["tw_military_time"] = false;

#ifdef TW_INCLUDE_CRYPTO
  persist_[TW_USE_SHA2] = true;
  persist_[TW_NO_SHA2] = false;
#else
  persist_[TW_NO_SHA2] = true;
#endif

#ifdef AB_OTA_UPDATER
  persist_[TW_UNMOUNT_SYSTEM] = false;
#else
  persist_[TW_UNMOUNT_SYSTEM] = true;
#endif

#if defined BOARD_USES_RECOVERY_AS_BOOT && defined BOARD_BUILD_SYSTEM_ROOT_IMAGE
  consts_["tw_uses_initramfs"] = true;
#else
  consts_["tw_uses_initramfs"] = false;
#endif

#if defined BOARD_USES_RECOVERY_AS_BOOT || defined BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT
  consts_["tw_include_install_recovery_ramdisk"] = true;
#else
  consts_["tw_include_install_recovery_ramdisk"] = false;
#endif

#ifdef BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT
  consts_["tw_is_vendor_boot"] = true;
#else
  consts_["tw_is_vendor_boot"] = false;
#endif

#ifdef TW_NO_SCREEN_TIMEOUT
  consts_["tw_screen_timeout_secs"] = false;
  consts_["tw_no_screen_timeout"] = true;
#else
  persist_["tw_screen_timeout_secs"] = 60;
  persist_["tw_no_screen_timeout"] = false;
#endif

#ifdef BOARD_BOOT_HEADER_VERSION
  consts_["tw_boot_header_version"] = BOARD_BOOT_HEADER_VERSION;
#endif

  consts_["tw_is_vendor_boot_header_v3"] = GetIntValue("tw_is_vendor_boot") == 1 &&
                                           GetIntValue("tw_boot_header_version") == 3;

  data_["tw_gui_done"] = false;
  data_["tw_encrypt_backup"] = false;
  data_["tw_sleep_total"] = 5;
  data_["tw_sleep"] = 5;
  data_["tw_enable_fastboot"] = false;

  if (android::base::GetBoolProperty("ro.virtual_ab.enabled", false))
    consts_[TW_VIRTUAL_AB_ENABLED]
        = true;
  else consts_[TW_VIRTUAL_AB_ENABLED] = false;

  HandleBrightnessConfig();

#ifdef TW_HAS_MTP
  consts_["tw_has_mtp"] = true;
  persist_["tw_mtp_enabled"] = true;
  persist_["tw_mtp_debug"] = false;
#else
  LOGINFO("TW_EXCLUDE_MTP := true\n");
  consts_["tw_has_mtp"] = false;
  consts_["tw_mtp_enabled"] = false;
#endif

  persist_["tw_mount_system_ro"] = 2;
  persist_["tw_never_show_system_ro_page"] = false;
  persist_["tw_language"] = std::string(TW_DEFAULT_LANGUAGE);
  LOGINFO("LANG: %s\n", EXPAND(TW_DEFAULT_LANGUAGE));

  data_["tw_has_adopted_storage"] = false;

#ifdef AB_OTA_UPDATER
  LOGINFO("AB_OTA_UPDATER := true\n");
  consts_["tw_has_boot_slots"] = true;
#else
  consts_["tw_has_boot_slots"] = false;
#endif

#ifndef TW_EXCLUDE_NANO
  consts_["tw_include_nano"] = true;
#else
  LOGINFO("TW_EXCLUDE_NANO := true\n");
  consts_["tw_include_nano"] = false;
#endif

  data_["tw_flash_both_slots"] = false;
  data_["tw_is_slot_part"] = false;

  data_["tw_enable_adb_backup"] = false;

  consts_["tw_logcat_exists"] = TWFunc::Path_Exists("/system/bin/logcat");
  consts_["tw_has_repack_tools"] = TWFunc::Path_Exists("/system/bin/magiskboot");

  pthread_mutex_unlock(&values_lock_);
}

void DataManager::HandleBrightnessConfig() {
  std::string brightness_path;
#ifdef TW_BRIGHTNESS_PATH
  brightness_path = TW_BRIGHTNESS_PATH;
  LOGINFO("TW_BRIGHTNESS_PATH := %s\n", TW_BRIGHTNESS_PATH);
  if (!TWFunc::Path_Exists(TW_BRIGHTNESS_PATH)) {
    LOGINFO("Specified brightness file '%s' not found.\n", TW_BRIGHTNESS_PATH);
    brightness_path.clear();
  }
#endif

  // Attempt to locate the brightness file
  if (brightness_path.empty()) {
    brightness_path = find_first_named_file("brightness", "/sys/class/backlight");
    if (brightness_path.empty()) {
      brightness_path = find_first_named_file("brightness", "/sys/class/leds/lcd-backlight");
    }
  }
  if (brightness_path.empty()) {
    LOGINFO("Unable to locate brightness file\n");
    consts_["tw_has_brightnesss_file"] = false;
    return;
  }

  LOGINFO("Found brightness file at '%s'\n", brightness_path.c_str());
  consts_["tw_has_brightnesss_file"] = true;
  consts_["tw_brightness_file"] = brightness_path;

  int max_brightness;
#ifdef TW_MAX_BRIGHTNESS
  max_brightness = TW_MAX_BRIGHTNESS;
#else
  // Derive the sibling max_brightness path without mutating brightness_path.
  const fs::path bpath(brightness_path);
  const std::string max_brightness_path =
      bpath.parent_path() / std::format("max_{}", bpath.filename());
  if (TWFunc::Path_Exists(max_brightness_path)) {
    if (android::base::ReadFileToString(max_brightness_path, &max_brightness)) {
      LOGINFO("Got max brightness %s from '%s'\n", max_brightness.c_str(),
              max_brightness_path.c_str());
    } else {
      // Something went wrong, set that to indicate error
      max_brightness = -1;
    }
  }
  // Fallback into default
  if (max_brightness <= 0) max_brightness = 255;
#endif
  consts_["tw_brightness_max"] = max_brightness;
  persist_["tw_brightness"] = max_brightness / 5;
  persist_["tw_brightness_pct"] = 20;

#ifdef TW_SECONDARY_BRIGHTNESS_PATH
  std::string second_brightness_path = EXPAND(TW_SECONDARY_BRIGHTNESS_PATH);
  if (!second_brightness_path.empty() && TWFunc::Path_Exists(second_brightness_path)) {
    LOGINFO("Will use a second brightness file at '%s'\n", second_brightness_path.c_str());
    consts_["tw_secondary_brightness_file"] = second_brightness_path;
  } else {
    LOGINFO("Specified secondary brightness file '%s' not found.\n",
            second_brightness_path.c_str());
  }
#endif

#ifdef TW_DEFAULT_BRIGHTNESS
  const int defPctInt = static_cast<double>(TW_DEFAULT_BRIGHTNESS) / max_brightness * 100;
  persist_["tw_brightness_pct"] = defPctInt;
  persist_["tw_brightness"] = TW_DEFAULT_BRIGHTNESS;
  TWFunc::Set_Brightness(std::to_string(TW_DEFAULT_BRIGHTNESS));
#else
  TWFunc::Set_Brightness(std::to_string(max_brightness / 5));
#endif
}

// Magic Values
int DataManager::GetMagicValue(const std::string& key, std::string& value) {
  // Handle special dynamic cases
  if (key == "tw_time") {
    const time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    int tw_military_time;
    GetValue(TW_MILITARY_TIME, tw_military_time);

    if (tw_military_time) {
      value = std::format("{:02}:{:02}", local.tm_hour, local.tm_min);
    } else {
      int hour12 = local.tm_hour % 12;
      if (hour12 == 0) hour12 = 12;
      value = std::format("{:02}:{:02} {}", hour12, local.tm_min,
                          local.tm_hour < 12 ? "AM" : "PM");
    }
    return 0;
  }
  if (key == "tw_cpu_temp") {
    int tw_no_cpu_temp;
    GetValue("tw_no_cpu_temp", tw_no_cpu_temp);
    if (tw_no_cpu_temp == 1) return -1;

    // Cached for 5s; steady_clock is monotonic, so wall-clock changes can't stall it.
    static std::chrono::steady_clock::time_point next_refresh{};
    static uint64_t convert_temp = 0;
    if (const auto now = std::chrono::steady_clock::now(); now > next_refresh) {
#ifdef TW_CUSTOM_CPU_TEMP_PATH
      const std::string cpu_temp_file = EXPAND(TW_CUSTOM_CPU_TEMP_PATH);
#else
      const std::string cpu_temp_file = "/sys/class/thermal/thermal_zone0/temp";
#endif
      std::string result;
      if (!android::base::ReadFileToString(cpu_temp_file, &result)) return -1;
      // std::from_chars partial-parses past the trailing newline sysfs appends, so
      // no Trim is needed; on garbage raw stays 0, like the old strtoul.
      uint64_t raw = 0;
      std::from_chars(result.data(), result.data() + result.size(), raw);
      convert_temp = raw / 1000;
      if (convert_temp == 0) convert_temp = raw;
      if (convert_temp >= 150) convert_temp = raw / 10;
      next_refresh = now + std::chrono::seconds(5);
    }
    value = std::to_string(convert_temp);
    return 0;
  }
  return -1;
}

void DataManager::OutputVersion() {
  const fs::path log_dir = TWFunc::get_log_dir();
  if (log_dir.empty()) {
    LOGINFO("Unable to find cache directory\n");
    return;
  }

  const fs::path recovery_log_dir = log_dir / "recovery";
  std::error_code ec;

  if (log_dir == CACHE_LOGS_DIR) {
    if (!PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, false)) {
      LOGINFO("Unable to mount '%s' to write version number.\n", log_dir.c_str());
      return;
    }

    if (!fs::exists(recovery_log_dir, ec)) {
      LOGINFO("Recreating %s folder.\n", recovery_log_dir.c_str());
      // Create_Dir_Recursive rather than fs::create_directories: it applies
      // mode/uid/gid to every newly created directory.
      if (!TWFunc::Create_Dir_Recursive(recovery_log_dir,
                                        S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP, 0, 0)) {
        LOGERR("DataManager::OutputVersion -- Unable to make %s: %s\n", recovery_log_dir.c_str(),
               strerror(errno));
        return;
      }
    }
  }

  const fs::path version_path = recovery_log_dir / ".version";
  fs::remove(version_path, ec);  // removing a missing file is a no-op, so no exists() guard needed
  if (const std::string version = TWFunc::Get_TWRP_Version_Str();
    !android::base::WriteStringToFile(version, version_path)) {
    LOGINFO("Unable to write version to: %s. Data may be unmounted. Error: %s\n",
            version_path.c_str(), strerror(errno));
    return;
  }
  // overwrite_existing + the error_code overload restore TWFunc::copy_file semantics:
  // without overwrite_existing, fs::copy_file throws "File exists" once the dest from a
  // previous boot is present, crashing recovery; the error_code overload stays
  // non-throwing (the original ignored a failed copy).
  fs::copy_file("/etc/recovery.fstab", recovery_log_dir / "recovery.fstab",
                fs::copy_options::overwrite_existing, ec);
  if (ec) {
    LOGINFO("Unable to copy recovery.fstab: %s\n", ec.message().c_str());
  }
  PartitionManager.Output_Storage_Fstab();
  sync();
  LOGINFO("Version number saved to '%s'\n", version_path.c_str());
}

void DataManager::ReadSettingsFile() {
  // Load up the values for TWRP - Sleep to let the card be ready
  const auto settings_file = fs::path(TW_PERSIST_DIR) / TW_SETTINGS_FILE;
  int is_enc, has_data_media;

  GetValue(TW_IS_ENCRYPTED, is_enc);
  GetValue(TW_HAS_DATA_MEDIA, has_data_media);

  /*
  if (!PartitionManager.Mount_Settings_Storage(false))
  {
    usleep(500000);
    if (!PartitionManager.Mount_Settings_Storage(false))
      gui_msg(Msg(msg::kError, "unable_to_mount=Unable to mount {1}")(settings_file));
  }

  mkdir(mkdir_path, 0777);
  */

  LOGINFO("Attempt to load settings from settings file...\n");
  LoadValues(settings_file);
  OutputVersion();
  PartitionManager.Mount_All_Storage();
  UpdateTimezoneEnvironment();
  TWFunc::Set_Brightness(GetStrValue("tw_brightness"));
}

std::string DataManager::GetCurrentStoragePath() {
  return GetStrValue("tw_storage_path");
}

std::string DataManager::GetSettingsStoragePath() {
  return GetStrValue("tw_settings_path");
}

void DataManager::Vibrate(const std::string& key) {
#ifndef TW_NO_HAPTICS
  int vib_value = 0;
  GetValue(key, vib_value);
  if (vib_value) vibrate(vib_value);
#endif
}

void DataManager::LoadTWRPFolderInfo() {
  SetValue(TW_RECOVERY_FOLDER_VAR, TWFunc::Check_For_TwrpFolder());
  kBackingFile = fs::path(TW_PERSIST_DIR) / TW_SETTINGS_FILE;
}
