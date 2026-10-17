/*
    Copyright 2013 to 2021 TeamWin
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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <mntent.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <ranges>
#include <sstream>
#include <string_view>
#include <thread>

#include <android-base/properties.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <selinux/selinux.h>
#include <selinux/label.h>
#include <sparse_format.h>
#include <sparse/sparse.h>

#include "libblkid/include/blkid.h"
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "progresstracking.hpp"
#include "mounts.h"
#include "data.hpp"
#include "twrp_functions.hpp"
#include "twrpTar.hpp"
#include "exclude.hpp"
#include "infomanager.hpp"
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "unit_conversion.hpp"

#ifdef TW_INCLUDE_CRYPTO
#include "Decrypt.h"
#endif

extern "C" {
#ifdef USE_EXT4
#include <ext4_utils/make_ext4fs.h>
#endif
}

#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>

// v2 fstab allows you to specify a mount point of "auto" with no /. These items are given a mount point of /auto* where * == auto_index
static int auto_index = 0;

extern selabel_handle* selinux_handle;
extern bool datamedia;

// 1 mebibyte; the bytes<->MiB unit conversion used throughout (e.g. static_cast<int>(bytes / kMiB)).
constexpr unsigned long long kMiB = 1024ULL * 1024;

// Upper bound for a fstab line, inherited from the legacy fixed buffer.
constexpr int kMaxFstabLineLength = 2048;

// atoi-style parse of external text (fstab values, command output): skips
// leading whitespace, no exceptions, garbage or empty -> 0.
template <typename T>
static T ParseOrZero(std::string_view text) {
  text.remove_prefix(std::min(text.find_first_not_of(" \t\r\n"), text.size()));
  T value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

struct FlagList {
  const char* name;
  unsigned flag;
};

// tw_flags[] maps flag names to the TWFlag discriminators consumed by
// ApplyTwFlag(); the null name terminates the table.
struct TwFlagEntry {
  const char* name;
  TWFlag flag;
};

// Unqualified k* names below refer to enum class TWFlag.
using enum TWFlag;

// Copy from system/core/init/builtins.cpp
constexpr FlagList mount_flags[] = {
  { "noatime", MS_NOATIME },
  { "noexec", MS_NOEXEC },
  { "nosuid", MS_NOSUID },
  { "nodev", MS_NODEV },
  { "nodiratime", MS_NODIRATIME },
  { "ro", MS_RDONLY },
  { "rw", 0 },
  { "remount", MS_REMOUNT },
  { "bind", MS_BIND },
  { "rec", MS_REC },
  { "unbindable", MS_UNBINDABLE },
  { "private", MS_PRIVATE },
  { "slave", MS_SLAVE },
  { "shared", MS_SHARED },
  { "nosymfollow", MS_NOSYMFOLLOW },
  { "defaults", 0 },
  { nullptr, 0 },
};

constexpr std::string ignored_mount_items[] = {
  "defaults=",
  "errors=",
  "latemount",
  "sysfs_path=",
};

/* Flags without a trailing '=' are considered dual format flags and can be
 * written as either 'flagname' or 'flagname=', where the character following
 * the '=' is Y,y,1 for true and false otherwise.
 */
constexpr TwFlagEntry tw_flags[] = {
  { "andsec", kAndSec },
  { "backup", kBackup },
  { "backupname=", kBackupName },
  { "blocksize=", kBlockSize },
  { "canbewiped", kCanBeWiped },
  { "canencryptbackup", kCanEncryptBackup },
  { "defaults", kDefaults },
  { "display=", kDisplay },
  { "encryptable=", kEncryptable },
  { "fileencryption=", kFileEncryption },
  { "metadata_encryption=", kMetadataEncryption },
  { "flashimg", kFlashImg },
  { "forceencrypt=", kForceEncrypt },
  { "fsflags=", kFsFlags },
  { "ignoreblkid", kIgnoreBlkid },
  { "length=", kLength },
  { "mounttodecrypt", kMountToDecrypt },
  { "quota", kQuota },
  { "removable", kRemovable },
  { "settingsstorage", kSettingsStorage },
  { "storage", kStorage },
  { "storagename=", kStorageName },
  { "subpartitionof=", kSubPartitionOf },
  { "symlink=", kSymlink },
  { "userdataencryptbackup", kUserDataEncryptBackup },
  { "usermrf", kUserMrf },
  { "wipeduringfactoryreset", kWipeDuringFactoryReset },
  { "wipeingui", kWipeInGui },
  { "slotselect", kSlotSelect },
  { "wait", kWait },
  { "verify", kVerify },
  { "check", kCheck },
  { "altdevice", kAltDevice },
  { "notrim", kNoTrim },
  { "voldmanaged=", kVoldManaged },
  { "formattable", kFormattable },
  { "resize", kResize },
  { "keydirectory=", kKeyDirectory },
  { "wrappedkey", kWrappedKey },
  { "adopted_mount_delay=", kAdoptedMountDelay },
  { "dm_use_original_path", kDmUseOriginalPath },
  { "fscompress", kFsCompress },
  { "logical", kLogical },
  { "metadata_csum", kMetadataCsum },
  { nullptr, static_cast<TWFlag>(0) },
};

TWPartition::TWPartition() {
  can_be_mounted_ = false;
  can_be_wiped_ = false;
  can_be_backed_up_ = false;
  use_rm_rf_ = false;
  wipe_during_factory_reset_ = false;
  wipe_available_in_gui_ = false;
  is_sub_partition_ = false;
  has_sub_partition_ = false;
  sub_partition_of_ = "";
  symlink_path_ = "";
  symlink_mount_point_ = "";
  mount_point_ = "";
  backup_path_ = "";
  wildcard_block_device_ = false;
  sysfs_entry_ = "";
  actual_block_device_ = "";
  primary_block_device_ = "";
  alternate_block_device_ = "";
  removable_ = false;
  is_present_ = false;
  length_ = 0;
  size_ = 0;
  used_ = 0;
  free_ = 0;
  backup_size_ = 0;
  backup_size_provisional_ = false;
  can_be_encrypted_ = false;
  is_encrypted_ = false;
  is_decrypted_ = false;
  is_fbe_ = false;
  mount_to_decrypt_ = false;
  decrypted_block_device_ = "";
  display_name_ = "";
  backup_display_name_ = "";
  storage_name_ = "";
  backup_name_ = "";
  backup_file_name_ = "";
  backup_method_ = BackupMethod::kNone;
  can_encrypt_backup_ = false;
  use_userdata_encryption_ = false;
  has_data_media_ = false;
  has_android_secure_ = false;
  is_storage_ = false;
  is_settings_storage_ = false;
  storage_path_ = "";
  current_file_system_ = "";
  fstab_file_system_ = "";
  mount_flags_ = 0;
  mount_options_ = "";
  format_block_size_ = 0;
  ignore_blkid_ = false;
  mtp_storage_id_ = 0;
  can_flash_img_ = false;
  mount_read_only_ = false;
  is_adopted_storage_ = false;
  slot_select_ = false;
  key_directory_ = "";
  is_super_ = false;
  needs_fs_compress_ = false;
  needs_metadata_csum_ = false;
}

static std::vector<std::string> SplitFstabLine(std::string_view line) {
  // Splits an fstab line into whitespace-separated tokens, honoring
  // double-quoted regions. A '"' toggles "inside-quote" state and is kept in
  // the token; bytes <= ' ' outside quotes separate tokens (consecutive
  // separators are skipped, matching the legacy null-byte-injection loop).
  // Input is bounded to kMaxFstabLineLength-1, the legacy strlcpy bound.
  if (line.size() > kMaxFstabLineLength - 1) line = line.substr(0, kMaxFstabLineLength - 1);

  std::vector<std::string> tokens;
  std::string cur;
  bool in_quote = false;
  for (const char c : line) {
    if (c == '"') {
      in_quote = !in_quote;
      cur.push_back(c);
    } else if (!in_quote && c <= ' ') {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) tokens.push_back(cur);
  return tokens;
}

static std::vector<std::string_view> SplitQuoted(const std::string_view s, const char sep) {
  // Splits a flags string on `sep`, honoring double-quoted regions: a `sep`
  // inside "..." is not a separator, and the '"' is kept in the token (to be
  // stripped later by the caller, matching the legacy separator->'\n' mark +
  // strtok_r path in ProcessTwFlags). Consecutive/leading/trailing
  // separators are coalesced (strtok_r semantics). Mirrors SplitFstabLine's
  // quote handling for a single separator char instead of whitespace.
  // Each token is a contiguous run of `s` (only separator chars are skipped,
  // never reordered), so tokens are returned as zero-copy string_views.
  std::vector<std::string_view> tokens;
  bool in_quote = false;
  size_t start = 0, i = 0;
  for (const char c : s) {
    if (c == '"') {
      in_quote = !in_quote;
    } else if (!in_quote && c == sep) {
      if (i > start) tokens.push_back(s.substr(start, i - start));
      start = i + 1;
    }
    ++i;
  }
  if (i > start) tokens.push_back(s.substr(start, i - start));
  return tokens;
}

bool TWPartition::ProcessFstabLine(const char* fstab_line, const bool display_error,
                                   std::map<std::string, FlagsMap>* twrp_flags) {
  const std::string_view line(fstab_line);
  // There can't possibly be a valid fstab line that is less than 10 chars
  if (line.size() < 10) return false;

  // skip comments
  if (line.starts_with('#')) return false;

  int fstab_version = 1;
  size_t mount_point_index = 0;
  size_t fs_index = 1;
  size_t block_device_index = 2;
  if (line.starts_with("/dev/") || line.starts_with("/devices/") || !line.starts_with('/')) {
    fstab_version = 2;
    block_device_index = 0;
    mount_point_index = 1;
    fs_index = 2;
  }

  const std::vector<std::string> tokens = SplitFstabLine(line);
  std::string twflags;
  TWPartition* additional_entry = nullptr;

  for (size_t i = 0; i < tokens.size(); i++) {
    const std::string& tok = tokens[i];
    if (i == mount_point_index) {
      mount_point_ = tok;
      if (fstab_version == 2 && !is_super_) {
        additional_entry = PartitionManager.FindPartitionByPath(mount_point_);
        if (additional_entry)
          LOGINFO("Found an additional entry for '%s'\n", mount_point_.c_str());
      }
      LOGINFO("Processing '%s'\n", mount_point_.c_str());
      backup_path_ = mount_point_;
      storage_path_ = mount_point_;
      display_name_ = tok.substr(1);
      backup_display_name_ = display_name_;
      storage_name_ = display_name_;
    } else if (i == fs_index) {
      // File System
      fstab_file_system_ = tok;
      current_file_system_ = tok;
    } else if (i == block_device_index) {
      // Primary Block Device
      primary_block_device_ = tok;
      if (tok.starts_with('/')) FindRealBlockDevice(primary_block_device_);
    } else if (i > 2) {
      if (fstab_version == 2) {
        if (i == 3) {
          ProcessFsFlags(tok);
          if (additional_entry) {
            additional_entry->SaveFsFlags(fstab_file_system_, mount_flags_, mount_options_);
            return false;
            // We save the extra fs flags in the other partition entry and by returning false, this entry will be deleted
          }
        } else {
          twflags = tok;
        }
      } else {
        if (tok.starts_with('/')) {
          // v2 fstab does not allow alternate block devices
          // Alternate Block Device
          alternate_block_device_ = tok;
          FindRealBlockDevice(alternate_block_device_);
        } else if (tok.size() > 7 && tok.starts_with("length=")) {
          // Partition length
          length_ = ParseOrZero<int>(tok.substr(7));
        } else if (tok.size() > 6 && tok.starts_with("flags=")) {
          // Custom flags, save for later so that new values aren't overwritten by defaults
          twflags = tok.substr(6);
        } else if (tok == "NULL" || tok == "null") {
          // Do nothing
        } else {
          // Unhandled data
          LOGINFO("Unhandled fstab information '%s' in fstab line '%s'\n", tok.c_str(), fstab_line);
        }
      }
    }
  }

  OverrideBlockDevicesFromFlags(fstab_version, display_error, twrp_flags);
  ApplyBlockDeviceAttributes(line);

  if (!ClassifyByMountPoint(display_error)) return false;

  // Process TWRP fstab flags
  if (!twflags.empty()) {
    const std::string Prev_Display_Name = display_name_;
    const std::string Prev_Storage_Name = storage_name_;
    const std::string Prev_Backup_Display_Name = backup_display_name_;
    display_name_ = "";
    storage_name_ = "";
    backup_display_name_ = "";

    ProcessTwFlags(twflags, fstab_version == 1, fstab_version);
    SaveFsFlags(fstab_file_system_, mount_flags_, mount_options_);

    const bool has_display_name = !display_name_.empty();
    const bool has_storage_name = !storage_name_.empty();
    const bool has_backup_name = !backup_display_name_.empty();
    if (!has_display_name) display_name_ = Prev_Display_Name;
    if (!has_storage_name) storage_name_ = Prev_Storage_Name;
    if (!has_backup_name) backup_display_name_ = Prev_Backup_Display_Name;

    if (has_display_name && !has_storage_name) storage_name_ = display_name_;
    if (!has_display_name && has_storage_name) display_name_ = storage_name_;
    if (has_display_name && !has_backup_name && backup_display_name_ !=
        "Android Secure") backup_display_name_ = display_name_;
    if (!has_display_name && has_backup_name) display_name_ = backup_display_name_;
  }

  ApplyTwrpFlagsFile(fstab_version, display_error, twrp_flags);
  MountPersistRootIfNeeded();

  return true;
}

void TWPartition::OverrideBlockDevicesFromFlags(const int fstab_version, bool display_error,
                                                std::map<std::string, FlagsMap>* twrp_flags) {
  // override block devices from the v2 fstab with the ones we read from the twrp.flags file in case they are different
  if (fstab_version == 2 && twrp_flags && !twrp_flags->empty()) {
    if (const auto it = twrp_flags->find(mount_point_); it != twrp_flags->end()) {
      if (!it->second.primary_block_device.empty()) {
        primary_block_device_ = it->second.primary_block_device;
        FindRealBlockDevice(primary_block_device_);
      }
      if (!it->second.alternate_block_device.empty()) {
        alternate_block_device_ = it->second.alternate_block_device;
        FindRealBlockDevice(alternate_block_device_);
      }
    }
  }
}

void TWPartition::ApplyBlockDeviceAttributes(std::string_view line) {
  if (line.starts_with("/devices/")) {
    sysfs_entry_ = primary_block_device_;
    primary_block_device_ = "";
    is_storage_ = true;
    removable_ = true;
    wipe_available_in_gui_ = true;
    wildcard_block_device_ = true;
  }
  if (primary_block_device_.find('*') != std::string::npos) wildcard_block_device_ = true;
}

bool TWPartition::ClassifyByMountPoint(bool display_error) {
  auto set_display_names = [this](const std::string& name) {
    display_name_ = name;
    backup_display_name_ = name;
    storage_name_ = name;
  };

  if (mount_point_ == "auto") {
    mount_point_ = "/auto";
    mount_point_ += std::to_string(auto_index);
    backup_path_ = mount_point_;
    storage_path_ = mount_point_;
    backup_name_ = mount_point_.substr(1);
    auto_index++;
    SetupFileSystem(display_error);
    set_display_names("Storage");
    can_be_backed_up_ = false;
    wipe_available_in_gui_ = true;
    is_storage_ = true;
    removable_ = true;
  } else if (!IsFileSystem(fstab_file_system_) && !IsImage(fstab_file_system_)) {
    if (display_error)
      LOGERR("Unknown File System: '%s'\n", fstab_file_system_.c_str());
    else
      LOGINFO("Unknown File System: '%s'\n", fstab_file_system_.c_str());
    return false;
  } else if (IsFileSystem(fstab_file_system_)) {
    FindActualBlockDevice();
    SetupFileSystem(display_error);
    backup_name_ = display_name_ = mount_point_.substr(1);
    if (mount_point_ == "/" || mount_point_ == "/system" || mount_point_ == "/system_root") {
      mount_point_ = TWPartitionManager::GetAndroidRootPath();
      backup_path_ = mount_point_;
      storage_path_ = mount_point_;
      set_display_names("System");
      backup_name_ = "system";
      wipe_available_in_gui_ = false;
      can_be_backed_up_ = false;
      can_be_wiped_ = false;
      MakeDir(TWPartitionManager::GetAndroidRootPath(), true);
    } else if (mount_point_ == "/system_ext") {
      set_display_names("System_EXT");
      backup_name_ = "System_EXT";
      can_be_backed_up_ = wipe_available_in_gui_ = !is_super_;
    } else if (mount_point_ == "/product") {
      set_display_names("Product");
      backup_name_ = "Product";
      can_be_backed_up_ = wipe_available_in_gui_ = !is_super_;
    } else if (mount_point_ == "/odm") {
      set_display_names("ODM");
      backup_name_ = "ODM";
      can_be_backed_up_ = wipe_available_in_gui_ = !is_super_;
    } else if (mount_point_ == "/data") {
      set_display_names("Data");
      wipe_available_in_gui_ = true;
      wipe_during_factory_reset_ = true;
      can_be_backed_up_ = true;
      can_encrypt_backup_ = true;
      use_userdata_encryption_ = true;
    } else if (mount_point_ == "/cache") {
      set_display_names("Cache");
      wipe_available_in_gui_ = true;
      wipe_during_factory_reset_ = true;
      can_be_backed_up_ = true;
    } else if (mount_point_ == "/datadata") {
      wipe_during_factory_reset_ = true;
      set_display_names("DataData");
      is_sub_partition_ = true;
      sub_partition_of_ = "/data";
      DataManager::SetValue(TW_HAS_DATADATA, 1);
      can_be_backed_up_ = true;
      can_encrypt_backup_ = true;
      use_userdata_encryption_ = false; // This whole partition should be encrypted
    } else if (mount_point_ == "/sd-ext") {
      wipe_during_factory_reset_ = true;
      set_display_names("SD-Ext");
      wipe_available_in_gui_ = true;
      removable_ = true;
      can_be_backed_up_ = true;
      can_encrypt_backup_ = true;
      use_userdata_encryption_ = true;
    } else if (mount_point_ == "/boot") {
      display_name_ = "Boot";
      backup_display_name_ = display_name_;
      DataManager::SetValue("tw_boot_is_mountable", 1);
      can_be_backed_up_ = true;
    } else if (mount_point_ == "/vendor") {
      set_display_names("Vendor");
    } else if (mount_point_ == "/metadata") {
      set_display_names("Metadata");
    } else if (mount_point_ == "/odm_dlkm") {
      set_display_names("ODM DLKM");
    } else if (mount_point_ == "/vendor_dlkm") {
      set_display_names("Vendor DLKM");
    }
#ifdef TW_EXTERNAL_STORAGE_PATH
    if (mount_point_ == TW_EXTERNAL_STORAGE_PATH) {
      is_storage_ = true;
      storage_path_ = TW_EXTERNAL_STORAGE_PATH;
      removable_ = true;
      wipe_available_in_gui_ = true;
#else
    if (mount_point_ == "/sdcard" || mount_point_ == "/external_sd" || mount_point_ ==
        "/external_sdcard") {
      is_storage_ = true;
      removable_ = true;
      wipe_available_in_gui_ = true;
#endif
    }
#ifdef TW_INTERNAL_STORAGE_PATH
    if (mount_point_ == TW_INTERNAL_STORAGE_PATH) {
      is_storage_ = true;
      is_settings_storage_ = true;
      storage_path_ = TW_INTERNAL_STORAGE_PATH;
      wipe_available_in_gui_ = true;
    }
#else
    if (mount_point_ == "/emmc" || mount_point_ == "/internal_sd" || mount_point_ ==
        "/internal_sdcard") {
      is_storage_ = true;
      is_settings_storage_ = true;
      wipe_available_in_gui_ = true;
    }
#endif
  } else if (IsImage(fstab_file_system_)) {
    FindActualBlockDevice();
    SetupImage();
    if (mount_point_ == "/boot") {
      display_name_ = "Boot";
      backup_display_name_ = display_name_;
      can_be_backed_up_ = true;
      can_flash_img_ = true;
    } else if (mount_point_ == "/init_boot") {
      display_name_ = "Init Boot";
      backup_display_name_ = display_name_;
      can_be_backed_up_ = true;
      can_flash_img_ = true;
    } else if (mount_point_ == "/recovery") {
      display_name_ = "Recovery";
      backup_display_name_ = display_name_;
      can_flash_img_ = true;
    } else if (mount_point_ == "/system_image") {
      display_name_ = "System Image";
      backup_display_name_ = display_name_;
      can_flash_img_ = true;
      can_be_backed_up_ = true;
    } else if (mount_point_ == "/vendor_image") {
      display_name_ = "Vendor Image";
      backup_display_name_ = display_name_;
      can_flash_img_ = true;
      can_be_backed_up_ = true;
    }
  }
  return true;
}

void TWPartition::ApplyTwrpFlagsFile(int fstab_version, bool display_error,
                                        std::map<std::string, FlagsMap>* twrp_flags) {
  if (fstab_version == 2 && twrp_flags && !twrp_flags->empty()) {
    auto it = twrp_flags->find(mount_point_);
    if (it != twrp_flags->end()) {
      std::string_view flagstr = it->second.flags;
      constexpr std::string_view flags_prefix = "flags=";
      if (flagstr.size() > flags_prefix.size() && flagstr.starts_with(flags_prefix)) flagstr.
          remove_prefix(flags_prefix.size());
      ProcessTwFlags(flagstr, display_error, 1);
      // Forcing the fstab to ver 1 because this data is coming from the /etc/twrp.flags which should be using the TWRP v1 flags format
    }
  }
}

void TWPartition::MountPersistRootIfNeeded() {
  if (mount_point_ == TW_PERSIST_ROOT && can_be_mounted_) {
    if (Mount(false)) {
      TWFunc::FixupTimeOnBoot(TW_PERSIST_ROOT "/time/");
      Unmount(false);
    }
  }
}

void TWPartition::PartitionPostProcessing(bool display_error) {
  if (mount_point_ == "/data") SetupDataPartition(display_error);
  else if (mount_point_ == "/cache") SetupCachePartition(display_error);
}

void TWPartition::ExcludeAll(const std::string& path) {
  backup_exclusions.add_absolute_dir(path);
  wipe_exclusions.add_absolute_dir(path);
}

void TWPartition::SetupDataPartition(bool display_error) {
  if (mount_point_ != "/data") return;

  // Ensure /data is not mounted as tmpfs for qcom hardware decrypt
  Unmount(false);

#ifdef TW_INCLUDE_CRYPTO
#ifdef TW_PREPARE_DATA_MEDIA_EARLY
  if (datamedia) SetupDataMedia();
#endif
  can_be_encrypted_ = true;
  const std::string crypto_blkdev = android::base::GetProperty("ro.crypto.fs_crypto_blkdev", "error");
  if (crypto_blkdev != "error") {
    SetFbeStatus();
    decrypted_block_device_ = crypto_blkdev;
    LOGINFO("Data already decrypted, new block device: '%s'\n", crypto_blkdev.c_str());
#ifndef TW_PREPARE_DATA_MEDIA_EARLY
    if (datamedia) SetupDataMedia();
#endif
    DataManager::SetValue(TW_IS_ENCRYPTED, 0);
  } else if (!Mount(false)) {
    is_encrypted_ = true;
    is_decrypted_ = false;
    if (datamedia) SetupDataMedia();
    if (key_directory_.empty()) {
      LOGERR("Primary block device '%s' for mount point '%s' is not present!\n",
             primary_block_device_.c_str(), mount_point_.c_str());
    }
  } else {
    SetFbeStatus();
    int is_device_fbe = 0;
    DataManager::GetValue(TW_IS_FBE, is_device_fbe);
    std::string crypto_state = android::base::GetProperty("ro.crypto.state", "error");
    if (!DecryptFbeDe() && crypto_state != "error") {
      if (is_device_fbe == 1) LOGERR("Unable to decrypt FBE device\n");
    } else {
      DataManager::SetValue(TW_IS_ENCRYPTED, 0);
#ifndef TW_PREPARE_DATA_MEDIA_EARLY
      if (datamedia) SetupDataMedia();
#endif
    }
  }
  if (datamedia && (!is_encrypted_ || (is_encrypted_ && is_decrypted_))) {
    SetupDataMedia();
    RecreateMediaFolder();
  }
#else
  if (datamedia) {
    SetupDataMedia();
    RecreateMediaFolder();
  }
#endif
}

void TWPartition::SetFbeStatus() {
  DataManager::SetValue(TW_IS_DECRYPTED, 1);
  is_encrypted_ = true;
  is_decrypted_ = true;
  if (key_directory_.empty()) {
    is_fbe_ = false;
    DataManager::SetValue(TW_IS_FBE, 0);
  } else {
    LOGINFO("SetupDataPartition::Key_Directory::%s\n", key_directory_.c_str());
    is_fbe_ = true;
    DataManager::SetValue(TW_IS_FBE, 1);
  }
}

bool TWPartition::DecryptFbeDe() {
  if (TWFunc::IsPathExists("/data/unencrypted/key/version")) {
    DataManager::SetValue(TW_IS_FBE, 1);
    TWPartitionManager::SetCryptoState();
    TWPartitionManager::SetCryptoType("file");
    LOGINFO("File Based Encryption is present\n");
#ifdef TW_INCLUDE_FBE
    is_fbe_ = true;
    ExcludeAll(mount_point_ + "/convert_fbe");
    ExcludeAll(mount_point_ + "/unencrypted");
    ExcludeAll(mount_point_ + "/misc/vold/user_keys");
    ExcludeAll(mount_point_ + "/misc/vold/volume_keys");
    ExcludeAll(mount_point_ + "/system/gatekeeper.password.key");
    ExcludeAll(mount_point_ + "/system/gatekeeper.pattern.key");
    ExcludeAll(mount_point_ + "/system/locksettings.db");
    ExcludeAll(mount_point_ + "/system/locksettings.db-wal");
    ExcludeAll(mount_point_ + "/misc/gatekeeper");
    ExcludeAll(mount_point_ + "/misc/keystore");
    ExcludeAll(mount_point_ + "/drm/kek.dat");
    ExcludeAll(mount_point_ + "/system_de/0/spblob");
    // contains data needed to decrypt synthetic password
    ExcludeAll(mount_point_ + "/system/users/0/gatekeeper.password.key");
    ExcludeAll(mount_point_ + "/system/users/0/gatekeeper.pattern.key");
    ExcludeAll(mount_point_ + "/cache");
    ExcludeAll(mount_point_ + "/per_boot"); // removed each boot by init
    ExcludeAll(mount_point_ + "/gsi"); // cow devices

    // Not retried: every attempt installs keys and touches the key directories
    // again, so a retry only widens the damage when the first one went wrong.
    if (android::keystore::Decrypt_DE()) {
      de_keys_ready_ = true;
      TWPartitionManager::SetCryptoState();
      is_encrypted_ = true;
      is_decrypted_ = false;
      DataManager::SetValue(TW_IS_ENCRYPTED, 1);
      std::string filename;
      int pwd_type = android::keystore::Get_Password_Type(0, filename);
      if (pwd_type < 0) {
        LOGERR("This TWRP does not have synthetic password decrypt support\n");
        pwd_type = 0; // default password
      }
      PartitionManager.ParseUsers(); // after load_all_de_keys() to parse_users
      for (const UsersStruct& user : *PartitionManager.GetUsersList()) {
        if (user.user_id != 0) {
          const std::string uid = std::to_string(user.user_id);
          ExcludeAll(mount_point_ + "/system_de/" + uid + "/spblob");
          ExcludeAll(mount_point_ + "/system/users/" + uid + "/gatekeeper.password.key");
          ExcludeAll(mount_point_ + "/system/users/" + uid + "/gatekeeper.pattern.key");
          ExcludeAll(mount_point_ + "/system/users/" + uid + "/locksettings.db");
          ExcludeAll(mount_point_ + "/system/users/" + uid + "/locksettings.db-wal");
        }
      }
      DataManager::SetValue(TW_CRYPTO_PWTYPE, pwd_type);
      DataManager::SetValue("tw_crypto_pwtype_0", pwd_type);
      DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
      DataManager::SetValue("tw_crypto_display", "");
      return true;
    }
#else
    LOGERR("FBE found but FBE support not present in TWRP\n");
#endif
  }
  DataManager::SetValue(TW_IS_FBE, 0);
  return false;
}

void TWPartition::SetupCachePartition(bool display_error __unused) {
  if (mount_point_ != "/cache") return;

  if (!Mount(true)) return;

  if (!TWFunc::IsPathExists("/cache/recovery/.")) {
    LOGINFO("Recreating /cache/recovery folder\n");
    if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
      LOGERR("Could not create /cache/recovery\n");
  }
}

void TWPartition::ProcessFsFlags(std::string_view str) {
  mount_options_ = "";

  // Avoid issues with potentially nested strtok by using Tokenize, which
  // coalesces runs of delimiter bytes and ignores leading/trailing ones
  // (matching the legacy strtok_r(options, ",", ...) semantics exactly).
  for (const std::string& tok : android::base::Tokenize(std::string(str), ",")) {
    const auto equals = tok.find('=');
    size_t name_len = equals == std::string::npos ? tok.size() : equals;

    // Skip flags TWRP ignores. The legacy strncmp(tok, *ignored, name_len)
    // is a PREFIX test on the name part (before '='), NOT full equality, so
    // substr(0, name_len) clamps to the shorter side (tokens and ignored
    // items have no embedded NULs, mirroring strncmp's null-stop behavior).
    if (std::ranges::any_of(ignored_mount_items, [&](const std::string& ignored_mount_item) {
      return tok.substr(0, name_len) == ignored_mount_item.substr(0, name_len);
    }))
      continue;

    // mount_flags are never postfixed by '='
    if (equals == std::string::npos) {
      // mount_flags is NULL-sentinel terminated (final {0,0}); unlike the
      // legacy for(;mount_flags[i].name;) loop, ranges::find_if walks the
      // full sized array INCLUDING the sentinel, so skip entries whose
      // name is null — otherwise tok == nullptr derefs strlen on the
      // sentinel and SIGSEGVs at boot (fault addr 0x0).
      const auto it = std::ranges::find_if(mount_flags, [&](const FlagList& mount_flag) {
        return mount_flag.name && tok == mount_flag.name;
      });
      if (it != std::ranges::end(mount_flags)) {
        if (it->flag == MS_RDONLY) mount_read_only_ = true;
        else mount_flags_ |= it->flag;
        continue;
      }
    }

    // If we aren't ignoring this flag and it's not a mount flag, then it must be a mount option
    if (!mount_options_.empty()) mount_options_ += ',';
    mount_options_ += tok;
  }
}

void TWPartition::SaveFsFlags(const std::string& local_File_System, int local_Mount_Flags,
                                const std::string& local_Mount_Options) {
  fs_flags.push_back({local_File_System, local_Mount_Flags, local_Mount_Options});
}

// fileencryption=/metadata_encryption= values are "contents[:filenames]"; publish them as
// <prefix>.contents / <prefix>.filenames properties, which vold reads on decrypt.
static void SetSplitProperty(const char* tag, const std::string& prefix, std::string_view value) {
  const std::string val(value);
  const size_t colon_loc = val.find(':');
  const std::string contents = val.substr(0, colon_loc);
  const std::string filenames = colon_loc == std::string::npos ? "" : val.substr(colon_loc + 1);
  android::base::SetProperty(prefix + ".contents", contents);
  android::base::SetProperty(prefix + ".filenames", filenames);
  LOGINFO("%s contents '%s', filenames '%s'\n", tag, contents.c_str(), filenames.c_str());
}

void TWPartition::ApplyTwFlag(const TWFlag flag, const std::string_view str, const bool val) {
  switch (flag) {
    case kAndSec:
      has_android_secure_ = val;
      break;
    case kBackup:
      can_be_backed_up_ = val;
      break;
    case kBackupName:
      backup_display_name_ = str;
      break;
    case kBlockSize:
      format_block_size_ = ParseOrZero<uint64_t>(str);
      break;
    case kCanBeWiped:
      can_be_wiped_ = val;
      break;
    case kCanEncryptBackup:
      can_encrypt_backup_ = val;
      break;
    case kDefaults:
    case kWait:
    case kVerify:
    case kCheck:
    case kNoTrim:
    case kVoldManaged:
    case kResize:
    case kEncryptable:
    case kForceEncrypt:
      // Do nothing
      break;
    case kDisplay:
      display_name_ = str;
      break;
    // This flag isn't used by TWRP but is needed in 9.0 FBE decrypt
    // fileencryption=ice:aes-256-heh
    case kFileEncryption:
      SetSplitProperty("FBE", "fbe", str);
      break;
    // This flag isn't used by TWRP but is needed for FBEv2 metadata decryption
    // metadata_encryption=aes-256-xts:wrappedkey_v0
    case kMetadataEncryption:
      SetSplitProperty("Metadata", "metadata", str);
      break;
    case kWrappedKey:
      // no more processing needed. leaving it here in case we want to do something in the future
      break;
    case kFlashImg:
      can_flash_img_ = val;
      break;
    case kFsFlags:
      ProcessFsFlags(str);
      break;
    case kIgnoreBlkid:
      ignore_blkid_ = val;
      break;
    case kLength:
      length_ = ParseOrZero<int>(str);
      break;
    case kMountToDecrypt:
      mount_to_decrypt_ = val;
      break;
    case kQuota:
      // Filesystem flag - TWRP does not need to process
      break;
    case kRemovable:
      removable_ = val;
      break;
    case kSettingsStorage:
      is_settings_storage_ = val;
      if (is_settings_storage_) is_storage_ = true;
      break;
    case kStorage:
      is_storage_ = val;
      break;
    case kStorageName:
      storage_name_ = str;
      break;
    case kSubPartitionOf:
      is_sub_partition_ = true;
      sub_partition_of_ = str;
      break;
    case kSymlink:
      symlink_path_ = str;
      break;
    case kUserDataEncryptBackup:
      use_userdata_encryption_ = val;
      if (use_userdata_encryption_) can_encrypt_backup_ = true;
      break;
    case kUserMrf:
      use_rm_rf_ = val;
      break;
    case kWipeDuringFactoryReset:
      wipe_during_factory_reset_ = val;
      if (wipe_during_factory_reset_) {
        can_be_wiped_ = true;
        wipe_available_in_gui_ = true;
      }
      break;
    case kWipeInGui:
    case kFormattable:
      wipe_available_in_gui_ = val;
      if (wipe_available_in_gui_) can_be_wiped_ = true;
      break;
    case kSlotSelect:
      slot_select_ = true;
      break;
    case kAltDevice:
      alternate_block_device_ = str;
      break;
    case kAdoptedMountDelay:
      // Recognized for compatibility; the parsed value is not used.
      break;
    case kKeyDirectory:
      key_directory_ = str;
      LOGINFO("setting Key_Directory to: %s\n", key_directory_.c_str());
      break;
    case kDmUseOriginalPath:
      // Recognized for compatibility; the flag is not used.
      break;
    case kLogical:
      is_super_ = true;
      break;
    case kFsCompress:
#ifdef TW_ENABLE_FS_COMPRESSION
      needs_fs_compress_ = true;
      LOGINFO("Enabling 'fs compression'\n");
#else
      LOGINFO("Ignoring the 'fscompress' fstab flag\n");
#endif
      break;
    case kMetadataCsum:
      needs_metadata_csum_ = true;
      break;
    default:
      // Should not get here
      LOGINFO("Flag identified for processing, but later unmatched: %i\n", flag);
      break;
  }
}

void TWPartition::ProcessTwFlags(const std::string_view flags, const bool display_error, const int fstab_ver) {
  // Semicolons within double-quotes are not separators, so split on `sep`
  // with quote awareness (mirrors the legacy separator->'\n' mark + strtok_r:
  // a sep inside "..." is kept, the '"' is kept in the token for the caller
  // to strip, and consecutive/leading/trailing separators are coalesced).
  for (const char sep = fstab_ver == 2 ? ',' : ';'; std::string_view t : SplitQuoted(flags, sep)) {
    const TwFlagEntry* tw_flag = tw_flags;

    for (; tw_flag->name; ++tw_flag) {
      std::string_view name(tw_flag->name);
      const size_t flag_len = name.size();
      if (!t.starts_with(name)) continue;

      // Reject prefix collisions: a bare flag whose name is a proper
      // prefix of the token but is not followed by '='
      // (e.g. "backup" vs "backupname=", "storage" vs "storagename=").
      if (t.size() > flag_len && !name.ends_with('=') && t[flag_len] != '=') continue;

      bool flag_val = false;
      std::string_view value;
      const bool has_equals = name.ends_with('=');

      if (t.size() == flag_len) {
        // Exact name match with no '=' payload.
        if (has_equals) {
          // "backupname=" — argument required but missing.
          LOGINFO("Flag missing argument: %s\n", tw_flag->name);
          break;
        }
        // Bare dual-format flag written without '=' (e.g. "backup").
        flag_val = true;
        value = t;
      } else if (t[flag_len] == '=') {
        // Dual-format flag given with '=' (e.g. "backup=y"): the '='
        // sits right after the bare name; the rest is the value.
        value = t.substr(flag_len + 1);
        android::base::ConsumePrefix(&value, "\"");
        android::base::ConsumeSuffix(&value, "\"");
        if (value.empty()) {
          // e.g. "backup="
          LOGINFO("Flag missing argument or should not include '=': %s=\n", tw_flag->name);
          break;
        }
        flag_val = std::string_view("yY1").find(value.front()) != std::string_view::npos;
      } else {
        // Value flag (name ends with '=') with its argument
        // (e.g. backupname="My Stuff").
        value = t.substr(flag_len);
        android::base::ConsumePrefix(&value, "\"");
        android::base::ConsumeSuffix(&value, "\"");
        if (value.empty()) {
          // e.g. backupname=""
          LOGINFO("Flag missing argument: %s\n", tw_flag->name);
          break;
        }
      }

      ApplyTwFlag(tw_flag->flag, value, flag_val);
      break;
    }
    if (!tw_flag->name) {
      if (display_error)
        LOGERR("Unhandled flag: '%s'\n", std::string(t).c_str());
      else
        LOGINFO("Unhandled flag: '%s'\n", std::string(t).c_str());
    }
  }
}

constexpr std::array fs_list = {
  "ext2", "ext3", "ext4", "vfat",
  "ntfs", "exfat", "f2fs", "mifs",
  "erofs", "squashfs", "auto",
};

bool TWPartition::IsFileSystem(const std::string_view file_system) {
  return std::ranges::find(fs_list, file_system) != fs_list.end();
}

bool TWPartition::IsImage(const std::string_view file_system) {
  return file_system == "emmc";
}

bool TWPartition::MakeDir(const std::string& path, const bool display_error) {
  if (TWFunc::GetDTypeFromStat(path) != DT_DIR) unlink(path.c_str());
  if (!TWFunc::IsPathExists(path)) {
    if (mkdir(path.c_str(), 0777) == -1) {
      if (display_error)
        gui_msg(
            Msg(msg::kError, "create_folder_strerr=Can not create '{1}' folder ({2}).")(path)(
                strerror(errno)));
      else
        LOGINFO("Can not create '%s' folder.\n", path.c_str());
      return false;
    }
    LOGINFO("Created '%s' folder.\n", path.c_str());
    return true;
  }
  return true;
}

void TWPartition::SetupFileSystem(bool display_error) {
  can_be_mounted_ = true;
  can_be_wiped_ = true;

  // Make the mount point folder if it doesn't exist
  MakeDir(mount_point_, display_error);
  backup_method_ = BackupMethod::kFiles;
}

void TWPartition::SetupImage() {
  display_name_ = mount_point_.substr(1);
  backup_name_ = display_name_;
  if (current_file_system_ == "emmc") backup_method_ = BackupMethod::kDd;
  else
    LOGINFO("Unhandled file system '%s' on image '%s'\n", current_file_system_.c_str(),
            display_name_.c_str());
}

void TWPartition::SetupAndSec() {
  backup_display_name_ = "Android Secure";
  backup_name_ = "and-sec";
  can_be_backed_up_ = true;
  has_android_secure_ = true;
  symlink_path_ = mount_point_ + "/.android_secure";
  symlink_mount_point_ = "/and-sec";
  backup_path_ = symlink_mount_point_;
  MakeDir("/and-sec", true);
  RecreateAndSecFolder();
  MountStorageRetry(true);
}

void TWPartition::SetupDataMedia() {
  LOGINFO("Setting up '%s' as data/media emulated storage.\n", mount_point_.c_str());
  if (storage_name_.empty() || storage_name_ == "Data") storage_name_ = "Internal Storage";
  has_data_media_ = true;
  is_storage_ = true;
  storage_path_ = mount_point_ + "/media";
  symlink_path_ = storage_path_;
  if (mount_point_ == "/data") {
    is_settings_storage_ = true;
#ifdef TW_EXTERNAL_STORAGE_PATH
    const bool external_storage_is_sdcard = std::string_view(TW_EXTERNAL_STORAGE_PATH) == "/sdcard";
#else
    constexpr bool external_storage_is_sdcard = false;
#endif
    if (external_storage_is_sdcard) {
      MakeDir("/emmc", false);
      symlink_mount_point_ = "/emmc";
    } else {
      MakeDir("/sdcard", false);
      symlink_mount_point_ = "/sdcard";
    }
    const std::string media0 = mount_point_ + "/media/0";
    if (const bool mount = Mount(false);
#ifdef TW_PREPARE_DATA_MEDIA_EARLY
      mount &&
#endif
      TWFunc::IsPathExists(media0)) {
      storage_path_ = media0;
      symlink_path_ = storage_path_;
      DataManager::SetValue(TW_INTERNAL_PATH, media0);
#ifndef TW_INCLUDE_CRYPTO
      DataManager::SetValue("tw_settings_path", TW_STORAGE_PATH);
#endif
      Unmount(true);
    }
    DataManager::SetValue("tw_has_internal", 1);
    DataManager::SetValue("tw_has_data_media", 1);
    backup_exclusions.add_absolute_dir("/data/data/com.google.android.music/files");
    backup_exclusions.add_absolute_dir("/data/per_boot");
    // DJ9,14Jan2020 - exclude this dir to prevent "error 255" on AOSP ROMs that create and lock it
    backup_exclusions.add_absolute_dir("/data/vendor/dumpsys");
    backup_exclusions.add_absolute_dir("/data/cache");
    backup_exclusions.add_absolute_dir("/data/misc/apexdata/com.android.art");
    // exclude this dir to prevent "error 255" on AOSP Android 12
    backup_exclusions.add_absolute_dir("/data/extm");
    //exclude this dir to prevent "error 255" on MIUI
    backup_exclusions.add_absolute_dir("/data/gsi");
    // Contains huge files (DSU System image + Userdata image), and won't work after restoration (requires configuration files in metadata)
    backup_exclusions.add_absolute_dir("/data/adb/ksu/modules.img");
    //After ksu 0.8.x the modules.img file became 1tb, which is inhibiting the execution of backups
    wipe_exclusions.add_absolute_dir(mount_point_ + "/misc/vold"); // adopted storage keys
    ExcludeAll(mount_point_ + "/system/storage.xml");
#ifdef TW_WORKAROUND_BACKUP_BUG
    backup_exclusions.add_absolute_dir("/data/data");
    // temporary workaround for error 255 when restoring data backups in 16 branch builds
#endif

    backup_exclusions.add_absolute_dir("/data/system/users/0/package-restrictions.xml");

    // board-customisable exclusions
#ifdef TW_BACKUP_EXCLUSIONS
    for (const std::string& extra_x : android::base::Tokenize(TW_BACKUP_EXCLUSIONS, ",")) {
      std::string s1 = android::base::Trim(extra_x);
      if (!s1.empty()) {
        backup_exclusions.add_absolute_dir(s1);
        LOGINFO("Adding user-defined path '%s' to the backup exclusions\n", s1.c_str());
      }
    }
#endif
  } else {
    for (const int i : std::views::iota(2, 10)) {
      if (auto path = fs::path("/sdcard") / std::to_string(i); !TWFunc::IsPathExists(path)) {
        MakeDir(path, false);
        symlink_mount_point_ = path;
        LOGINFO("'%s' data/media emulated storage symlinked to %s.\n", mount_point_.c_str(),
                symlink_mount_point_.c_str());
        break;
      }
    }
    if (Mount(true) && TWFunc::IsPathExists(mount_point_ + "/media/0")) {
      storage_path_ = mount_point_ + "/media/0";
      symlink_path_ = storage_path_;
      Unmount(true);
    }
  }
  ExcludeAll(mount_point_ + "/media");
}

void TWPartition::FindRealBlockDevice(string &block_device) {
  // Preserve by-name for slot_select suffix logic before canonical resolves it away.
  original_block_device_ = block_device;
  std::error_code ec;
  if (const auto resolved = fs::canonical(block_device, ec); !ec) {
    block_device = resolved.string();
  }
}

bool TWPartition::MountStorageRetry(const bool display_error) {
  // On some devices, storage doesn't want to mount right away, retry and sleep
  if (!Mount(display_error)) {
    int retry_count = 5;
    while (retry_count > 0 && !Mount(false)) {
      usleep(500000);
      retry_count--;
    }
    return Mount(display_error);
  }
  return true;
}

bool TWPartition::GetSizeViaStatfs(const bool display_error) {
  if (!Mount(display_error)) return false;

  struct statfs st{};
  const auto local_path = fs::path(mount_point_);
  if (statfs(local_path.c_str(), &st) != 0) {
    if (!removable_) {
      if (display_error)
        LOGERR("Unable to statfs '%s'\n", local_path.c_str());
      else
        LOGINFO("Unable to statfs '%s'\n", local_path.c_str());
    }
    return false;
  }
  size_ = st.f_blocks * st.f_bsize;
  used_ = (st.f_blocks - st.f_bfree) * st.f_bsize;
  free_ = st.f_bfree * st.f_bsize;
  backup_size_ = used_;
  return true;
}

bool TWPartition::GetSizeViaDf(const bool display_error) {
  if (!Mount(display_error)) return false;

  // df default output, one row per filesystem with sizes in 1K-blocks:
  //   Filesystem 1K-blocks Used Available Use% Mounted on
  //   <device>  <size>   <used> <avail>  <%>  <mountpoint>
  // When the device name overflows its column, df writes the name alone on one
  // line and the numeric fields on an indented continuation line.
  // Capture df's stdout directly through a pipe instead of a temp file: the
  // previous /tmp/dfoutput.txt was a fixed path that concurrent calls clobber.
  std::string output;
  const std::string command = std::format("df {}", mount_point_);
  // ExecCmd streams df's stdout into `output` via a pipe and already logs a
  // popen failure (LOGERR); an empty or unparseable result is caught by the
  // parse loop below, which returns false. Gating on the return code is
  // avoided because pclose() may return -1 (e.g. EINTR) after output was read.
  TWFunc::ExecCmd(command, output, false);

  std::string line;
  std::istringstream iss(output);
  while (std::getline(iss, line)) {
    if (line.starts_with("Filesystem")) continue; // header row

    uint64_t blocks = 0, used = 0, available = 0;
    int parsed;
    // A continuation row is indented (the device column is empty), so it is
    // parsed from its first number. A normal row begins with the device
    // name, which %*s discards before the three numeric fields. The lone
    // device name on a wrapped row leaves no number for %lu to read, so it
    // falls through (parsed != 3) and its continuation is handled next loop.
    if (line.starts_with(' ') || line.starts_with('\t')) parsed = sscanf(line.c_str(),
                                                           "%lu %lu %lu", &blocks, &used,
                                                           &available);
    else parsed = sscanf(line.c_str(), "%*s %lu %lu %lu", &blocks, &used, &available);

    if (parsed != 3) continue;

    // df reports sizes in 1K-blocks; convert to bytes.
    size_ = blocks * 1024ULL;
    used_ = used * 1024ULL;
    free_ = available * 1024ULL;
    backup_size_ = used_;
    return true; // df returns a single row for this mount point
  }

  LOGINFO("Unable to parse df output for '%s'.\n", mount_point_.c_str());
  return false;
}

uint64_t TWPartition::IoctlGetBlockSize() {
  FindActualBlockDevice();

  return TWFunc::GetBlockSizeByIoctl(actual_block_device_.c_str());
}

bool TWPartition::FindPartitionSize() {
  if (const uint64_t ioctl_size = IoctlGetBlockSize()) {
    size_ = ioctl_size;
    return true;
  }

  return false;
}

bool TWPartition::IsMounted() const {
  if (!can_be_mounted_) return false;

  // Check to see if the mount point directory exists
  struct stat st1{};
  if (stat((mount_point_ + "/.").c_str(), &st1) != 0) return false;

  // Check to see if the directory above the mount point exists
  struct stat st2{};
  if (stat((mount_point_ + "/../.").c_str(), &st2) != 0) return false;

  // Check to see if a symlink mount point exists and is mounted
  if (!symlink_mount_point_.empty()) {
    scan_mounted_volumes();
    if (find_mounted_volume_by_mount_point(symlink_mount_point_.c_str())) return true;
  }

  // st_dev differs from the parent directory's when a filesystem is mounted at
  // mount_point_; std::filesystem exposes no device id, so stat() is required here.
  return st1.st_dev != st2.st_dev;
}

bool TWPartition::IsFileSystemWritable() const {
  if (!IsFileSystem(current_file_system_) || !IsMounted()) return false;

  // access(W_OK) honors the mount's read-only flag and the real uid/gid;
  // std::filesystem::permissions() checks only mode bits and would misreport.
  const std::string test_path = mount_point_ + "/.";
  return access(test_path.c_str(), W_OK) == 0;
}

bool TWPartition::RestoreDeKeys() {
#ifdef TW_INCLUDE_FBE
  if (is_fbe_ && mount_point_ == "/data" && !de_keys_ready_) {
    if (!android::keystore::Decrypt_DE()) {
      LOGERR("Unable to restore DE keys on /data\n");
      return false;
    }
    de_keys_ready_ = true;
  }
#endif
  return true;
}

bool TWPartition::Mount(const bool display_error) {
  bool exfat_mounted = false;
  unsigned int flags = mount_flags_;

  if (IsMounted()) {
    return true;
  }
  if (!can_be_mounted_) {
    return false;
  }

  // Check the current file system before mounting
  CheckFsType();
  if (current_file_system_ == "exfat") {
    const std::string cmd = std::format("/system/bin/mount -t exfat {} {}", actual_block_device_,
                                        mount_point_);
    LOGINFO("cmd: %s\n", cmd.c_str());
    std::string result;
    if (TWFunc::ExecCmd(cmd, result, false) != 0) {
      LOGINFO("exfat failed to mount with result '%s', trying vfat\n", result.c_str());
      current_file_system_ = "vfat";
    } else {
      exfat_mounted = true;
    }
  }

  if (current_file_system_ == "ntfs" && !TWFunc::IsPathExists("/sys/module/tntfs") && (
        TWFunc::IsPathExists("/system/bin/ntfs-3g") || TWFunc::IsPathExists(
            "/system/bin/mount.ntfs"))) {
    std::string ntfsmount_binary;

    if (TWFunc::IsPathExists("/system/bin/ntfs-3g")) ntfsmount_binary = "ntfs-3g";
    else ntfsmount_binary = "mount.ntfs";

    // -o ro is appended for read-only mounts; empty for read-write.
    const std::string_view ro_flag = mount_read_only_ ? " -o ro" : "";
    const std::string cmd = std::format("/system/bin/{}{} {} {}", ntfsmount_binary, ro_flag,
                                        actual_block_device_, mount_point_);
    LOGINFO("cmd: '%s'\n", cmd.c_str());

    if (TWFunc::ExecCmd(cmd) == 0) {
      return true;
    }
    LOGINFO("ntfs-3g failed to mount, trying regular mount method.\n");
  } else {
    if (current_file_system_ == "ntfs" &&
        TWFunc::IsPathExists("/sys/module/tntfs")) current_file_system_ = "tntfs";
  }

  if (mount_read_only_) flags |= MS_RDONLY;

  std::string mount_fs = current_file_system_;
  if (current_file_system_ == "exfat" &&
      TWFunc::IsPathExists("/sys/module/texfat")) mount_fs = "texfat";

  if (!exfat_mounted &&
      mount(actual_block_device_.c_str(), mount_point_.c_str(), mount_fs.c_str(), flags,
            mount_options_.c_str()) != 0 &&
      mount(actual_block_device_.c_str(), mount_point_.c_str(), mount_fs.c_str(), flags,
            nullptr) != 0) {
    if (!removable_ && display_error) gui_msg(
        Msg(msg::kError, "fail_mount=Failed to mount '{1}' ({2})")(mount_point_)(strerror(errno)));
    else
      LOGINFO("Unable to mount '%s'\n", mount_point_.c_str());

    LOGINFO("Actual block device: '%s', current file system: '%s'\n", actual_block_device_.c_str(),
            current_file_system_.c_str());
    return false;
  }

  if (removable_) UpdateSize(display_error);

  if (!symlink_mount_point_.empty()) {
    if (!BindMount()) return false;
  }
  return true;
}

bool TWPartition::BindMount() const {
  if (TWFunc::IsPathExists(symlink_path_)) {
    if (mount(symlink_path_.c_str(), symlink_mount_point_.c_str(), "", MS_BIND, nullptr) < 0) {
      LOGINFO("Failed to bind mount '%s' onto '%s': %s\n", symlink_path_.c_str(),
              symlink_mount_point_.c_str(), strerror(errno));
      return false;
    }
  }
  return true;
}

bool TWPartition::EnsureSubdirectoryUnmounted(const char* mount_point) {
  std::unique_ptr<FILE, decltype(&endmntent)> mnts(setmntent("/proc/mounts", "r"), endmntent);
  if (!mnts) {
    LOGERR("Could not read /proc/mounts before unmount\n");
    return false;
  }

  // Trailing slash makes starts_with() match children ("/data/foo") but skip the mount point itself.
  std::string top_directory(mount_point);
  if (!top_directory.ends_with('/')) top_directory += '/';

  std::vector<std::string> umount_points;
  for (mntent* mentry = getmntent(mnts.get()); mentry; mentry = getmntent(mnts.get())) {
    if (top_directory == mentry->mnt_dir) continue;
    if (std::string_view{mentry->mnt_dir}.starts_with(top_directory)) {
      LOGINFO("Found sub-directory mount '%s' under '%s'\n", mentry->mnt_dir, mount_point);
      umount_points.emplace_back(mentry->mnt_dir);
    }
  }

  // Unmount deepest paths first so nested mounts detach before their parents.
  std::ranges::sort(umount_points, [](const std::string& a, const std::string& b) {
    return a.size() > b.size();
  });

  for (const std::string& sub_mount : umount_points) {
    LOGINFO("Unmounting sub-directory mount '%s'\n", sub_mount.c_str());
    if (umount(sub_mount.c_str()) != 0) {
      // A parent overmount can hide an older child still listed in /proc/mounts.
      if (errno == EINVAL || errno == ENOENT) continue;
      LOGERR("Failed to unmount '%s': '%s'\n", sub_mount.c_str(), strerror(errno));
      return false;
    }
  }
  return true;
}

static bool IsPathMounted(const std::string& path) {
  std::unique_ptr<FILE, decltype(&endmntent)> mounts(setmntent("/proc/mounts", "r"), endmntent);
  if (!mounts) return false;
  while (mntent* entry = getmntent(mounts.get())) {
    if (path == entry->mnt_dir) return true;
  }
  return false;
}

static void StopAsyncDataSize();

bool TWPartition::Unmount(const bool display_error, const int flags) const {
  if (!IsMounted()) return true;

  // A running size walk keeps /data busy and must not outlive its unmount.
  if (mount_point_ == "/data") StopAsyncDataSize();

  int never_unmount_system = 0;
  DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
  // Never unmount the Android root when configured to retain it.
  if (never_unmount_system == 1 && mount_point_ == TWPartitionManager::GetAndroidRootPath())
    return true;

  // /data/user/0 is a bind mount on /data/data; unmounting the parent takes it
  // with it, so remember whether it has to come back if we bail out.
  const bool had_user_bind = mount_point_ == "/data" && IsPathMounted("/data/user/0");
  const auto restore_user_bind = [&] {
    if (had_user_bind && !IsPathMounted("/data/user/0") &&
        mount("/data/data", "/data/user/0", nullptr, MS_BIND, nullptr) != 0) {
      LOGERR("Unable to restore /data/user/0: %s\n", strerror(errno));
    }
  };
  if (!EnsureSubdirectoryUnmounted(mount_point_.c_str())) {
    restore_user_bind();
    return false;
  }

  if (is_storage_ && mtp_storage_id_ > 0) TWPartitionManager::RemoveMtpStorage(mtp_storage_id_);

  if (!symlink_mount_point_.empty()) umount2(symlink_mount_point_.c_str(), flags);

  // Capture errno before IsMounted()'s stat() clobbers it.
  int umount_err = umount2(mount_point_.c_str(), flags) != 0 ? errno : 0;
  // EBUSY is almost always a process holding open files under the mount;
  // kill the holders (recovery itself is excluded) and retry once.
  if (IsMounted() && umount_err == EBUSY) {
    TWFunc::KillForUseTargetProcess(mount_point_);
    if (!symlink_mount_point_.empty()) TWFunc::KillForUseTargetProcess(symlink_mount_point_);
    // Re-sweep submounts: their first unmount may have failed while held.
    EnsureSubdirectoryUnmounted(mount_point_.c_str());
    umount_err = umount2(mount_point_.c_str(), flags) != 0 ? errno : 0;
  }
  if (!IsMounted()) {
    // Unmounting Data drops the fscrypt keys, so the mount no longer holds a
    // decryptable view of /data; lock it back instead of reporting a decrypt
    // that is no longer true.  Unmount() is const, and the lock state is the
    // point of the call, so the cast is deliberate.
    if (is_fbe_ && mount_point_ == "/data") {
      const_cast<TWPartition*>(this)->de_keys_ready_ = false;
      PartitionManager.MarkDataLocked();
    }
    return true;
  }
  // The parent is still mounted: restore storage access removed above rather
  // than leaving /sdcard and MTP detached on failure.
  restore_user_bind();
  if (!symlink_mount_point_.empty() && !IsPathMounted(symlink_mount_point_)) {
    if (!BindMount()) LOGERR("Unable to restore bind mount '%s'\n", symlink_mount_point_.c_str());
  }
  if (is_storage_ && mtp_storage_id_ > 0) TWPartitionManager::AddMtpStorage(mtp_storage_id_);
  if (display_error)
    gui_msg(Msg(msg::kError, "fail_unmount=Failed to unmount '{1}' ({2})")(mount_point_)(
        strerror(umount_err)));
  else
    LOGINFO("Unable to unmount '%s'\n", mount_point_.c_str());
  return false;
}

bool TWPartition::Remount(const bool display_error) {
  if (Unmount(display_error)) return Mount(display_error);
  return false;
}

bool TWPartition::RemountRw(const bool display_error) {
  // No need to remount if already mounted rw
  if (IsFileSystemWritable()) return true;

  const bool ro = mount_read_only_;
  const int flags = mount_flags_;

  mount_read_only_ = false;
  mount_flags_ &= ~MS_RDONLY;

  const bool ret = Remount(display_error);

  mount_read_only_ = ro;
  mount_flags_ = flags;

  return ret;
}

bool TWPartition::BlkDiscard() const {
  LOGINFO("Perform BLKDISCARD on block device %s\n", actual_block_device_.c_str());
  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(actual_block_device_.c_str(), O_WRONLY)));
  if (!fd.ok()) {
    LOGINFO("BlkDiscard: open failed: %s\n", strerror(errno));
    return false;
  }
  uint64_t size = 0;
  if (ioctl(fd.get(), static_cast<int>(BLKGETSIZE64), &size) == -1) {
    LOGINFO("BlkDiscard: BLKGETSIZE64 failed: %s\n", strerror(errno));
    return false;
  }
  if (size == 0) return false;
  // range is {start_byte, length_byte}; discard the whole device.
  uint64_t range[2] = {0, size};
  if (ioctl(fd.get(), BLKDISCARD, &range) == -1) {
    LOGINFO("BlkDiscard: BLKDISCARD failed: %s\n", strerror(errno));
    return false;
  }
  return true;
}

bool TWPartition::Wipe(const std::string& new_file_system) {
  bool wiped = false, update_crypt = false;

  if (!can_be_wiped_) {
    gui_msg(Msg(msg::kError, "cannot_wipe=Partition {1} cannot be wiped.")(display_name_));
    return false;
  }

  if (mount_point_ == TWPartitionManager::GetAndroidRootPath()) {
    if (tw_get_default_metadata(TWPartitionManager::GetAndroidRootPath().c_str()) != 0) {
      gui_msg(Msg(msg::kWarning,
                  "restore_system_context=Unable to get default context for {1} -- Android may not boot.")(
          TWPartitionManager::GetAndroidRootPath()));
    }
  }

  if (has_data_media_ && current_file_system_ == new_file_system) {
    wiped = WipeDataWithoutWipingMedia();
    if (mount_point_ == "/data" && TWFunc::GetLogDir() == DATA_LOGS_DIR) {
      if (!TWPartitionManager::RecreateLogsDir())
        LOGERR("Unable to create log directory for TWRP\n");
    }
  } else {
    int check = 0;
    DataManager::GetValue(TW_RM_RF_VAR, check);
    if (check || use_rm_rf_) wiped = WipeRmrf();
    else if (new_file_system == "ext4") wiped = WipeExt4();
    else if (new_file_system == "ext2" || new_file_system ==
             "ext3") wiped = WipeExtfs(new_file_system);
    else if (new_file_system == "exfat") wiped = WipeExfat();
    else if (new_file_system == "ntfs" || current_file_system_ == "tntfs") wiped = WipeNtfs();
    else if (new_file_system == "f2fs" || current_file_system_ == "mifs") wiped = WipeF2fs();
    else if (new_file_system == "vfat") wiped = WipeFat();
    else {
      LOGERR("Unable to wipe '%s' -- unknown file system '%s'\n", mount_point_.c_str(),
             new_file_system.c_str());
      return false;
    }
    update_crypt = false;
    // intentionally disabled: fscrypt (AOSP 10) made this crypt-refresh path obsolete; the `if (update_crypt)` block below is retained but inert.
  }

  if (wiped) {
    if (mount_point_ == "/cache" && TWFunc::GetLogDir() !=
        DATA_LOGS_DIR) DataManager::OutputVersion();

    if (mount_point_ == TWPartitionManager::GetAndroidRootPath()) {
      tw_set_default_metadata(TWPartitionManager::GetAndroidRootPath().c_str());
    }
    if (update_crypt) {
      SetupFileSystem(false);
      if (is_encrypted_ && !is_decrypted_) {
        // just wiped an encrypted partition back to its unencrypted state
        is_encrypted_ = false;
        is_decrypted_ = false;
        decrypted_block_device_ = "";
        if (mount_point_ == "/data") {
          DataManager::SetValue(TW_IS_ENCRYPTED, 0);
          DataManager::SetValue(TW_IS_DECRYPTED, 0);
        }
      }
    }

    if (is_storage_ && Mount(false) && !is_fbe_) TWPartitionManager::AddMtpStorage(mtp_storage_id_);
  }

  return wiped;
}

bool TWPartition::Wipe() {
  if (IsFileSystem(current_file_system_)) return Wipe(current_file_system_);
  return Wipe(fstab_file_system_);
}

bool TWPartition::WipeAndSec() {
  if (!has_android_secure_) return false;

  if (!Mount(true)) return false;

  gui_msg(Msg("wiping=Wiping {1}")(backup_display_name_));
  TWFunc::RemoveDir(mount_point_ + "/.android_secure/", true);
  return true;
}

bool TWPartition::WipeDataCache() {
  if (!Mount(true)) return false;
  gui_msg(Msg("wiping=Wiping {1}")(mount_point_ + "/cache/"));
  TWFunc::RemoveDir(mount_point_ + "/cache/", true);
  return true;
}

bool TWPartition::CanRepair() const {
  if (mount_read_only_) return false;
  if (current_file_system_ == "vfat" && TWFunc::IsPathExists("/system/bin/fsck.fat")) return true;
  if ((current_file_system_ == "ext2" || current_file_system_ == "ext3" || current_file_system_ ==
       "ext4") &&
      TWFunc::IsPathExists("/system/bin/e2fsck"))
    return true;
  if (current_file_system_ == "exfat" && TWFunc::IsPathExists("/system/bin/fsck.exfat")) return true;
  if ((current_file_system_ == "f2fs" || current_file_system_ == "mifs") &&
      TWFunc::IsPathExists("/system/bin/fsck.f2fs"))
    return true;
  if ((current_file_system_ == "ntfs" || current_file_system_ == "tntfs") && (
        TWFunc::IsPathExists("/system/bin/ntfsfix") ||
        TWFunc::IsPathExists("/system/bin/fsck.ntfs")))
    return true;
  return false;
}

bool TWPartition::Repair() {
  // Each filesystem's repair follows the same shape — verify the fsck binary exists, unmount,
  // run it, report — differing only in binary name and flags. ntfs/tntfs picks between
  // ntfsfix and fsck.ntfs at runtime, then calls in with no flags.
  auto do_repair = [&](const char* binary, const char* flags, const char* error_name) -> bool {
    std::string full = "/system/bin/";
    full += binary;
    if (!TWFunc::IsPathExists(full)) {
      gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")(error_name));
      return false;
    }
    if (!Unmount(true)) return false;
    gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(display_name_)(binary));
    FindActualBlockDevice();
    std::string command = full;
    if (*flags) {
      command += ' ';
      command += flags;
    }
    command += ' ';
    command += actual_block_device_;
    LOGINFO("Repair command: %s\n", command.c_str());
    if (TWFunc::ExecCmd(command) == 0) {
      gui_msg("done=Done.");
      return true;
    }
    gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(display_name_));
    return false;
  };

  if (current_file_system_ == "vfat") return do_repair("fsck.fat", "-y", "fsck.fat");
  if (current_file_system_ == "ext2" || current_file_system_ == "ext3" || current_file_system_ ==
      "ext4") return do_repair("e2fsck", "-fp", "e2fsck");
  if (current_file_system_ == "exfat") return do_repair("fsck.exfat", "", "fsck.exfat");
  if (current_file_system_ == "f2fs" || current_file_system_ == "mifs") return do_repair(
      "fsck.f2fs", "-a", "fsck.f2fs");
  if (current_file_system_ == "ntfs" || current_file_system_ == "tntfs") {
    std::string ntfsfix_binary;
    if (TWFunc::IsPathExists("/system/bin/ntfsfix")) ntfsfix_binary = "ntfsfix";
    else if (TWFunc::IsPathExists("/system/bin/fsck.ntfs")) ntfsfix_binary = "fsck.ntfs";
    else {
      gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("ntfsfix"));
      return false;
    }
    return do_repair(ntfsfix_binary.c_str(), "", "ntfsfix");
  }
  return false;
}

bool TWPartition::CanResize() const {
  if (mount_read_only_) return false;
  if ((current_file_system_ == "ext2" || current_file_system_ == "ext3" || current_file_system_ ==
       "ext4") &&
      TWFunc::IsPathExists("/system/bin/resize2fs"))
    return true;
  return false;
}

bool TWPartition::Resize() {
  std::string command;

  if (current_file_system_ == "ext2" || current_file_system_ == "ext3" || current_file_system_ ==
      "ext4") {
    if (!CanRepair()) {
      LOGINFO("Cannot resize %s because %s cannot be repaired before resizing.\n",
              display_name_.c_str(),
              display_name_.c_str());
      gui_msg(Msg(msg::kError, "cannot_resize=Cannot resize {1}.")(display_name_));
      return false;
    }
    if (!TWFunc::IsPathExists("/system/bin/resize2fs")) {
      LOGINFO("resize2fs does not exist! Cannot resize!\n");
      gui_msg(Msg(msg::kError, "cannot_resize=Cannot resize {1}.")(display_name_));
      return false;
    }
    // Repair will unmount so no need to do it twice
    gui_msg(Msg("repair_resize=Repairing {1} before resizing.")(display_name_));
    if (!Repair()) return false;
    gui_msg(Msg("resizing=Resizing {1} using {2}...")(display_name_)("resize2fs"));
    FindActualBlockDevice();
    command = "/system/bin/resize2fs " + actual_block_device_;
    if (length_ != 0) {
      unsigned long long actual_size = IoctlGetBlockSize();
      if (actual_size == 0) return false;

      unsigned long long block_count;
      if (length_ < 0) {
        // Reduce overall size by this length
        block_count = actual_size / 1024LLU - length_ * -1 / 1024LLU;
      } else {
        // This is the size, not a size reduction
        block_count = length_ / 1024LLU;
      }
      command += " " + std::to_string(block_count) + "K";
    }
    LOGINFO("Resize command: %s\n", command.c_str());
    if (TWFunc::ExecCmd(command) == 0) {
      UpdateSize(true);
      gui_msg("done=Done.");
      return true;
    }
    UpdateSize(true);
    gui_msg(Msg(msg::kError, "unable_resize=Unable to resize {1}.")(display_name_));
    return false;
  }
  return false;
}

bool TWPartition::Backup(PartitionSettings* part_settings, pid_t* tar_fork_pid) {
  switch (backup_method_) {
    case BackupMethod::kFiles:
      return BackupTar(part_settings, tar_fork_pid);
    case BackupMethod::kDd:
      return BackupImage(part_settings);
    default:
      LOGERR("Unknown backup method for '%s'\n", mount_point_.c_str());
      return false;
  }
}

bool TWPartition::Restore(PartitionSettings* part_settings) {
  TWFunc::GuiOperationText(TW_RESTORE_TEXT, display_name_, gui_parse_text("{@restoring_hdr}"));
  LOGINFO("Restore filename is: %s/%s\n", part_settings->backup_folder.c_str(),
          backup_file_name_.c_str());

  std::string restore_file_system = GetRestoreFileSystem(part_settings);

  if (IsFileSystem(restore_file_system)) return RestoreTar(part_settings);
  if (IsImage(restore_file_system)) return RestoreImage(part_settings);

  LOGERR("Unknown restore method for '%s'\n", mount_point_.c_str());
  return false;
}

std::string TWPartition::GetRestoreFileSystem(PartitionSettings* part_settings) {
  std::string restore_file_system;

  // Parse backup filename to extract the file system before wiping
  const size_t first_period = backup_file_name_.find('.');
  if (first_period == std::string::npos) {
    LOGERR("Unable to find file system (first period).\n");
    return {};
  }
  restore_file_system = backup_file_name_.substr(first_period + 1,
                                               backup_file_name_.size() - first_period - 1);
  const size_t second_period = restore_file_system.find('.');
  if (second_period == std::string::npos) {
    LOGERR("Unable to find file system (second period).\n");
    return {};
  }
  restore_file_system.resize(second_period);
  LOGINFO("Restore file system is: '%s'.\n", restore_file_system.c_str());
  return restore_file_system;
}

std::string TWPartition::BackupMethodByName() const {
  switch (backup_method_) {
    case BackupMethod::kNone:
      return "none";
    case BackupMethod::kFiles:
      return "files";
    case BackupMethod::kDd:
      return "dd";
    default:
      return "undefined";
  }
}

bool TWPartition::WipeEncryption() {
  const bool save_data_media = has_data_media_;
  bool ret = false;
#ifdef TW_USE_DMCTL
  const char* userdata_mapper = "/dev/block/mapper/userdata";
#endif
  const std::unique_ptr<BasePartition> base_partition(MakePartition());

  if (!base_partition->PreWipeEncryption()) return ret;

  FindActualBlockDevice();
  if (!is_present_) {
    LOGINFO("Block device not present, cannot format %s.\n", display_name_.c_str());
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }

#ifdef TW_INCLUDE_CRYPTO
  if (!Unmount(true)) {
    LOGINFO("Force unmount /data.\n");
    TWFunc::KillForUseTargetProcess(symlink_mount_point_);
    if (!symlink_mount_point_.empty()) umount2(symlink_mount_point_.c_str(), MNT_FORCE);
    TWFunc::KillForUseTargetProcess(mount_point_);
    Unmount(false);
    if (IsMounted()) return false;
  }
#ifdef TW_USE_DMCTL
  if (mount_point_ == "/data" && TWFunc::IsPathExists(userdata_mapper)) {
    LOGINFO("Removing metadata-encryption userdata mapping before format.\n");
    int dmctl_result = TWFunc::ExecCmd("dmctl delete userdata", false);
    if (dmctl_result != 0 && TWFunc::IsPathExists(userdata_mapper)) {
      LOGERR("Unable to remove metadata-encryption userdata mapping.\n");
      return false;
    }
    for (int retry = 0;
         retry < 100 && TWFunc::IsPathExists(userdata_mapper);
         retry++)
      usleep(10000);
    if (TWFunc::IsPathExists(userdata_mapper)) {
      LOGERR("Metadata-encryption userdata mapping did not disappear before format.\n");
      return false;
    }
  }
#endif

#endif
  has_data_media_ = false;
  decrypted_block_device_ = "";
  is_decrypted_ = false;
  is_encrypted_ = false;
  if (Wipe(fstab_file_system_)) {
    has_data_media_ = save_data_media;
    DataManager::SetValue(TW_IS_ENCRYPTED, 0);
    gui_msg("format_data_msg=You may need to reboot recovery to be able to use /data again.");
    if (is_fbe_) {
      gui_msg(Msg(msg::kWarning,
                  "data_media_fbe_msg=TWRP will not recreate /data/media on an FBE device. Please reboot into your rom to create /data/media."));
    } else {
      if (has_data_media_ && !symlink_mount_point_.empty()) {
        if (Mount(false)) TWPartitionManager::AddMtpStorage(mtp_storage_id_);
      }
    }

    ret = true;
    if (!key_directory_.empty()) ret = PartitionManager.WipeByPath(key_directory_);
    if (ret) ret = base_partition->PostWipeEncryption();
    return ret;
  }
  has_data_media_ = save_data_media;
  gui_err("format_data_err=Unable to format to remove encryption.");
  if (has_data_media_ && Mount(false)) TWPartitionManager::AddMtpStorage(mtp_storage_id_);
  return ret;
}

void TWPartition::CheckFsType() {
  const char* type;

  // Skip probing when explicitly disabled for this partition
  if (ignore_blkid_) return;

  FindActualBlockDevice();
  if (!is_present_) return;

  const auto pr = blkid_new_probe_from_filename(actual_block_device_.c_str());
  // /storage probes a directory, so NULL is routine here, and
  // blkid_do_fullprobe() does not check what it is handed.
  if (!pr) {
    LOGINFO("Can't probe device %s\n", actual_block_device_.c_str());
    return;
  }
  // Free the probe on every exit path from here (fullprobe fail, lookup fail, success).
  auto pr_guard = android::base::make_scope_guard([&] {
    blkid_free_probe(pr);
  });
  if (blkid_do_fullprobe(pr)) {
    LOGINFO("Can't probe device %s\n", actual_block_device_.c_str());
    return;
  }

  if (blkid_probe_lookup_value(pr, "TYPE", &type, nullptr) < 0) {
    LOGINFO("can't find filesystem on device %s\n", actual_block_device_.c_str());
    return;
  }

  current_file_system_ = type;
  if (fs_flags.size() > 1) {
    auto found = std::ranges::find(fs_flags, current_file_system_,
                                   &PartitionFsFlagsStruct::file_system);
    // If we don't find a match, we default the flags to the first set of flags that we received from the fstab
    if (found == fs_flags.end()) found = fs_flags.begin();
    if (mount_flags_ != found->mount_flags || mount_options_ != found->mount_options) {
      mount_flags_ = found->mount_flags;
      mount_options_ = found->mount_options;
      LOGINFO("Mount_Flags: %i, Mount_Options: %s\n", mount_flags_, mount_options_.c_str());
    }
  }
}

bool TWPartition::WipeExtfs(const std::string& file_system) {
  if (!Unmount(true)) return false;

  if (!TWFunc::IsPathExists("/system/bin/mke2fs") || !
      TWFunc::IsPathExists("/system/bin/e2fsdroid")) return WipeRmrf();

  FindActualBlockDevice();
  if (!is_present_) {
    LOGINFO("Block device not present, cannot wipe %s.\n", display_name_.c_str());
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }

  const uint64_t dev_sz = TWFunc::GetBlockSizeByIoctl(actual_block_device_.c_str());
  if (!dev_sz) return false;

  const std::string size_str = std::to_string(dev_sz / 4096);

  gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(display_name_)("mke2fs"));

  // Execute mke2fs to create empty ext4 filesystem
  std::string cmd = std::format("mke2fs -t {} -b 4096 -I 512", file_system);
  if (needs_metadata_csum_) {
    cmd += " -O metadata_csum,64bit,extent";
  }
  cmd += " " + actual_block_device_ + " " + size_str;
  LOGINFO("mke2fs command: %s\n", cmd.c_str());
  int ret = TWFunc::ExecCmd(cmd);
  if (ret) {
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }

  // e2fsdroid presence was already checked at the top of this function.
  const std::string file_contexts_entry = (mount_point_ == "/system_root" ? "/" : mount_point_);
  char* secontext = nullptr;
  if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, file_contexts_entry.c_str(),
                                        S_IFDIR) < 0) {
    LOGINFO("Cannot lookup security context for '%s'\n", mount_point_.c_str());
  } else {
    freecon(secontext);
    // Execute e2fsdroid to initialize selinux context
    if (mount_point_ == TW_PERSIST_ROOT) {
      Mount(true);
      TWFunc::RemoveDir(TW_PERSIST_ROOT "/lost+found", false);
      Unmount(true);
    }
    cmd = std::format("e2fsdroid -e -S /file_contexts -a {} {}", file_contexts_entry, actual_block_device_);
    LOGINFO("e2fsdroid command: %s\n", cmd.c_str());
    ret = TWFunc::ExecCmd(cmd);
    if (ret) {
      gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
      return false;
    }
  }

  current_file_system_ = file_system;
  RecreateAndSecFolder();
  gui_msg("done=Done.");
  return true;
}

bool TWPartition::WipeExt4() {
#ifdef USE_EXT4
  int ret;

  if (!Unmount(true)) return false;

  FindActualBlockDevice();
  if (!is_present_) {
    LOGINFO("Block device not present, cannot wipe %s.\n", display_name_.c_str());
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }

  unsigned long long dev_sz = TWFunc::GetBlockSizeByIoctl(actual_block_device_.c_str());
  if (!dev_sz) return false;

  char* secontext = nullptr;

  gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(display_name_)("make_ext4fs"));

  if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, mount_point_.c_str(), S_IFDIR) <
      0) {
    LOGINFO("Cannot lookup security context for '%s'\n", mount_point_.c_str());
    ret = make_ext4fs(actual_block_device_.c_str(), dev_sz, mount_point_.c_str(), nullptr);
  } else {
    freecon(secontext);
    ret = make_ext4fs(actual_block_device_.c_str(), dev_sz, mount_point_.c_str(), selinux_handle);
  }
  if (ret != 0) {
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  } else {
    std::string sedir = mount_point_ + "/lost+found";
    PartitionManager.MountByPath(sedir.c_str(), true);
    rmdir(sedir.c_str());
    mkdir(sedir.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
    return true;
  }
#else
  return WipeExtfs("ext4");
#endif
}

bool TWPartition::WipeFat() {
  if (!Unmount(true)) return false;

  if (TWFunc::IsPathExists("/system/bin/mkfs.fat")) {
    gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(display_name_)("mkfs.fat"));
    FindActualBlockDevice();
    std::string cmd = "mkfs.fat " + actual_block_device_;
    if (TWFunc::ExecCmd(cmd) == 0) {
      current_file_system_ = "vfat";
      RecreateAndSecFolder();
      gui_msg("done=Done.");
      return true;
    }
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }
  return WipeRmrf();
}

bool TWPartition::WipeExfat() {
  if (!Unmount(true)) return false;

  if (TWFunc::IsPathExists("/system/bin/mkfs.exfat")) {
    gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(display_name_)("mkfs.exfat"));
    FindActualBlockDevice();
    if (const std::string cmd = "mkfs.exfat " + actual_block_device_; TWFunc::ExecCmd(cmd) == 0) {
      RecreateAndSecFolder();
      gui_msg("done=Done.");
      return true;
    }
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }
  return false;
}

bool TWPartition::WipeRmrf() {
  if (!Mount(true)) return false;

  // This is the only wipe that leaves the partition mounted, so we
  // must manually remove the partition from MTP if it is a storage
  // partition.
  if (is_storage_) TWPartitionManager::RemoveMtpStorage(mtp_storage_id_);

  gui_msg(Msg("remove_all=Removing all files under '{1}'")(mount_point_));
  TWFunc::RemoveDir(mount_point_, true);
  RecreateAndSecFolder();
  return true;
}

bool TWPartition::WipeF2fs() {
  std::string f2fs_command;

  if (!Unmount(true)) return false;

  if (TWFunc::IsPathExists("/system/bin/make_f2fs")) f2fs_command =
                                                     "/system/bin/make_f2fs -g android";
  else {
    LOGINFO("make_f2fs binary not found, using rm -rf to wipe.\n");
    return WipeRmrf();
  }

  bool needs_casefold = false;

  FindActualBlockDevice();
  if (!is_present_) {
    LOGINFO("Block device not present, cannot wipe %s.\n", display_name_.c_str());
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }

  if (mount_point_ == "/data") {
    needs_casefold = android::base::GetBoolProperty("external_storage.casefold.enabled", false);
  }

  const uint64_t dev_sz = TWFunc::GetBlockSizeByIoctl(actual_block_device_.c_str());
  if (!dev_sz) return false;

  // Project ID
  f2fs_command += " -O project_quota,extra_attr";

  if (needs_casefold) f2fs_command += " -O casefold -C utf8";

  if (needs_fs_compress_) f2fs_command += " -O compression,extra_attr";

  f2fs_command += " " + actual_block_device_ + " " + std::to_string(dev_sz / 4096);

  if (TWFunc::IsPathExists("/system/bin/sload_f2fs")) {
    f2fs_command += " && sload_f2fs -t /data " + actual_block_device_;
  }

  LOGINFO("make_f2fs command: %s\n", f2fs_command.c_str());

  if (TWFunc::ExecCmd(f2fs_command) == 0) {
    RecreateAndSecFolder();
    gui_msg("done=Done.");
    return true;
  }
  gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
  return false;
}

bool TWPartition::WipeNtfs() {
  std::string ntfsmake_binary;

  if (!Unmount(true)) return false;

  if (TWFunc::IsPathExists("/system/bin/mkntfs")) ntfsmake_binary = "mkntfs";
  else if (TWFunc::IsPathExists("/system/bin/mkfs.ntfs")) ntfsmake_binary = "mkfs.ntfs";
  else return false;

  gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(display_name_)(ntfsmake_binary));
  FindActualBlockDevice();
  if (const std::string cmd = "/system/bin/" + ntfsmake_binary + " " + actual_block_device_; TWFunc::ExecCmd(cmd) == 0) {
    RecreateAndSecFolder();
    gui_msg("done=Done.");
    return true;
  } else {
    gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(display_name_));
    return false;
  }
}

bool TWPartition::WipeDataWithoutWipingMedia() {
  bool ret = false;

  if (!Mount(true)) return false;

  gui_msg("wiping_data=Wiping data without wiping /data/media ...");
  ret = WipeDataWithoutWipingMediaFunc(mount_point_);
  if (ret) gui_msg("done=Done.");
  return ret;
}

bool TWPartition::WipeDataWithoutWipingMediaFunc(const std::filesystem::path& parent) {
  std::error_code it_ec;
  fs::directory_iterator it(parent, it_ec);
  if (it_ec) {
    gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(mount_point_)(
        it_ec.message()));
    return false;
  }
  for (const auto& entry : it) {
    const fs::path& path = entry.path();
    if (wipe_exclusions.check_skip_dirs(path.string())) {
      LOGINFO("skipped '%s'\n", path.c_str());
      continue;
    }

    // symlink_status (lstat) matches the old DT_DIR d_type test: is_directory() would
    // follow symlinks and wipe through a link into an excluded tree.
    if (std::error_code type_ec; fs::symlink_status(path, type_ec).type() == fs::file_type::directory) {
      if (!WipeDataWithoutWipingMediaFunc(path)) {
        return false;
      }
#ifdef TW_WORKAROUND_BACKUP_BUG
      if (path == "/data/data")
        // temporary workaround for error 255 when restoring data backups in 16 branch builds
        LOGINFO("DEBUG: TWRP: skipped /data/data/\n");
      else
#endif
      fs::remove(path, type_ec);  // rmdir; ENOTEMPTY expected when excluded children remain
    } else {
      // unlink every non-directory entry (files, symlinks, fifos, sockets; DT_UNKNOWN
      // resolved by the lstat above)
      if (!fs::remove(path, type_ec))
        LOGINFO("Unable to unlink '%s': %s\n", path.c_str(), type_ec.message().c_str());
    }
  }
  return true;
}

bool TWPartition::BackupTar(PartitionSettings* part_settings, pid_t* tar_fork_pid) {
  twrpTar tar;

  if (!Mount(true)) return false;

  TWFunc::GuiOperationText(TW_BACKUP_TEXT, backup_display_name_, gui_parse_text("{@backing}"));
  gui_msg(Msg("backing_up=Backing up {1}...")(backup_display_name_));

  DataManager::GetValue(TW_USE_COMPRESSION_VAR, tar.use_compression);

  if (can_encrypt_backup_) {
    DataManager::GetValue("tw_encrypt_backup", tar.use_encryption);
    if (tar.use_encryption) {
      if (use_userdata_encryption_) tar.userdata_encryption = tar.use_encryption;
      std::string Password;
      DataManager::GetValue("tw_backup_password", Password);
      tar.setpassword(Password);
    } else {
      tar.use_encryption = 0;
    }
  }

  backup_file_name_ = std::format("{}.{}.win", backup_name_, current_file_system_);
  const std::string full_filename = fs::path(part_settings->backup_folder) / backup_file_name_;
  if (has_data_media_)
#ifdef TW_WORKAROUND_BACKUP_BUG
  gui_msg(Msg(msg::kWarning,
              "backup_storage_warning=Backups of {1} do not include any files in internal storage such as pictures or downloads.")(
      display_name_));
#else
    LOGERR(
      "Backups of %s are currently BROKEN in the 16 branch. There is little point in making backups of %s\n\n",
      display_name_.c_str(), display_name_.c_str());
#endif

  if (mount_point_ == "/data" && DataManager::GetIntValue(TW_IS_FBE)) {
    for (const UsersStruct& user : *PartitionManager.GetUsersList()) {
      if (!user.is_decrypted && user.user_id != 0) {
        gui_msg(Msg(msg::kWarning,
                    "backup_storage_undecrypt_warning=Backup will not include some files from user {1} "
                    "because the user is not decrypted.")(user.user_id));
        const std::string uid = std::to_string(user.user_id);
        backup_exclusions.add_absolute_dir(fs::path("/data/system_ce") / uid);
        backup_exclusions.add_absolute_dir(fs::path("/data/misc_ce") / uid);
        backup_exclusions.add_absolute_dir(fs::path("/data/vendor_ce") / uid);
        backup_exclusions.add_absolute_dir(fs::path("/data/media") / uid);
        backup_exclusions.add_absolute_dir(fs::path("/data/user") / uid);
      }
    }
  }
  tar.part_settings = part_settings;
  tar.backup_exclusions = &backup_exclusions;
  tar.setdir(backup_path_);
  tar.setfn(full_filename);
  tar.setsize(backup_size_);
  tar.partition_name = backup_name_;
  tar.backup_folder = part_settings->backup_folder;
  if (tar.createTarFork(tar_fork_pid) != 0) return false;
  return true;
}

bool TWPartition::BackupImage(PartitionSettings* part_settings) {
  TWFunc::GuiOperationText(TW_BACKUP_TEXT, display_name_, gui_parse_text("{@backing}"));
  gui_msg(Msg("backing_up=Backing up {1}...")(backup_display_name_));

  backup_file_name_ = std::format("{}.{}.win", backup_name_, current_file_system_);

  std::string full_filename = fs::path(part_settings->backup_folder) / backup_file_name_;

  part_settings->total_restore_size = backup_size_;

  return RawReadWrite(part_settings);
}

bool TWPartition::RawReadWrite(const PartitionSettings* part_settings) const {
  uint64_t remain = backup_size_;
  uint64_t backed_up_size = 0;
  std::string srcfn, destfn;

  if (part_settings->pm_method == PartitionManagerOp::kBackup) {
    srcfn = actual_block_device_;
    destfn = fs::path(part_settings->backup_folder) / backup_file_name_;
  } else {
#ifdef TW_ENABLE_BLKDISCARD
    BlkDiscard();
#endif
    destfn = actual_block_device_;
    srcfn = fs::path(part_settings->backup_folder) / backup_file_name_;
    remain = TWFunc::GetFileSize(srcfn);
  }

  const android::base::unique_fd src_fd(open(srcfn.c_str(), O_RDONLY | O_LARGEFILE));
  if (!src_fd.ok()) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(srcfn)(
            strerror(errno)));
    return false;
  }

  const android::base::unique_fd dest_fd(
      open(destfn.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, S_IRUSR | S_IWUSR));
  if (!dest_fd.ok()) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(destfn)(
            strerror(errno)));
    return false;
  }

  LOGINFO("Reading '%s', writing '%s'\n", srcfn.c_str(), destfn.c_str());

  ssize_t bs = kMiB;

  const std::unique_ptr<char[]> buffer(new(std::nothrow) char[static_cast<size_t>(bs)]());
  if (!buffer) {
    LOGINFO("RawReadWrite failed to malloc\n");
    return false;
  }

  if (part_settings->progress) part_settings->progress->SetPartitionSize(
      part_settings->total_restore_size);

  while (remain > 0) {
    if (remain < kMiB) bs = static_cast<ssize_t>(remain);
    if (read(src_fd.get(), buffer.get(), bs) != bs) {
      LOGINFO("Error reading source fd (%s)\n", strerror(errno));
      return false;
    }
    if (write(dest_fd.get(), buffer.get(), bs) != bs) {
      LOGINFO("Error writing destination fd (%s)\n", strerror(errno));
      return false;
    }
    backed_up_size += bs;
    remain -= bs;
    if (part_settings->progress) part_settings->progress->UpdateSize(backed_up_size);
    if (PartitionManager.CheckBackupCancel() != 0) return false;
  }
  if (part_settings->progress) part_settings->progress->UpdateDisplayDetails(true);
  fsync(dest_fd.get());

  if (part_settings->pm_method == PartitionManagerOp::kBackup) {
    tw_set_default_metadata(destfn.c_str());
    LOGINFO("Restored default metadata for %s\n", destfn.c_str());
  }

  return true;
}

uint64_t TWPartition::GetRestoreSize(PartitionSettings* part_settings) {
  if (InfoManager restore_info(fs::path(part_settings->backup_folder) / std::format("{}.info", backup_name_));
    restore_info.LoadValues() == 0) {
    if (const std::optional<uint64_t> restore_size = restore_info["backup_size"]) {
      restore_size_ = restore_size.value();
      LOGINFO("Read info file, restore size is %lu\n", restore_size_);
      return restore_size_;
    }
  }

  const std::string full_filename = fs::path(part_settings->backup_folder) / backup_file_name_;
  const std::string restore_file_system = GetRestoreFileSystem(part_settings);

  if (IsImage(restore_file_system)) {
    restore_size_ = TWFunc::GetFileSize(full_filename);
    return restore_size_;
  }

  twrpTar tar;
  tar.setdir(backup_path_);
  tar.setfn(full_filename);
  tar.backup_name = full_filename;
  std::string Password;
  DataManager::GetValue("tw_restore_password", Password);
  if (!Password.empty()) tar.setpassword(Password);
  tar.partition_name = backup_name_;
  tar.backup_folder = part_settings->backup_folder;
  tar.part_settings = part_settings;
  restore_size_ = tar.get_size();
  return restore_size_;
}

bool TWPartition::RestoreTar(PartitionSettings* part_settings) {
  bool ret = false;
  std::string restore_file_system = GetRestoreFileSystem(part_settings);

  if (has_android_secure_) {
    if (!WipeAndSec()) return false;
  } else {
    gui_msg(Msg("wiping=Wiping {1}")(backup_display_name_));
    if (has_data_media_ && mount_point_ == "/data" && restore_file_system != current_file_system_) {
      gui_msg(Msg(msg::kWarning,
                  "datamedia_fs_restore=WARNING: This /data backup was made with {1} file system! The backup may not boot unless you change back to {1}.")(
          restore_file_system));
      if (!WipeDataWithoutWipingMedia()) return false;
    } else {
      if (!Wipe(restore_file_system)) return false;
    }
  }
  TWFunc::GuiOperationText(TW_RESTORE_TEXT, backup_display_name_,
                           gui_parse_text("{@restoring_hdr}"));
  gui_msg(Msg("restoring=Restoring {1}...")(backup_display_name_));

  // Remount as read/write as needed so we can restore the backup
  if (!RemountRw(true)) return false;

  const std::string full_filename = part_settings->backup_folder + "/" + backup_file_name_;
  twrpTar tar;
  tar.part_settings = part_settings;
  tar.setdir(backup_path_);
  tar.setfn(full_filename);
  tar.backup_name = backup_name_;
  std::string Password;
  DataManager::GetValue("tw_restore_password", Password);
  if (!Password.empty()) tar.setpassword(Password);
  part_settings->progress->SetPartitionSize(GetRestoreSize(part_settings));
  ret = (tar.extractTarFork() == 0);
  // Restore capabilities to the run-as binary
  if (mount_point_ == TWPartitionManager::GetAndroidRootPath() && Mount(true)
      && TWFunc::IsPathExists("/system/bin/run-as")) {
    vfs_cap_data cap_data{};
    constexpr uint64_t capabilities = (1 << CAP_SETUID) | (1 << CAP_SETGID);

    cap_data.magic_etc = VFS_CAP_REVISION | VFS_CAP_FLAGS_EFFECTIVE;
    cap_data.data[0].permitted = static_cast<uint32_t>(capabilities & 0xffffffff);
    cap_data.data[0].inheritable = 0;
    cap_data.data[1].permitted = static_cast<uint32_t>(capabilities >> 32);
    cap_data.data[1].inheritable = 0;
    if (setxattr("/system/bin/run-as", XATTR_NAME_CAPS, &cap_data, sizeof(cap_data), 0) < 0) {
      LOGINFO("Failed to reset capabilities of /system/bin/run-as binary.\n");
    } else {
      LOGINFO("Reset capabilities of /system/bin/run-as binary successful.\n");
    }
  }
  // Remount as read only when restoration is complete
  if (mount_read_only_ || (mount_flags_ & MS_RDONLY)) Remount(true);

  return ret;
}

bool TWPartition::RestoreImage(PartitionSettings* part_settings) {
  const std::string restore_file_system = GetRestoreFileSystem(part_settings);

  TWFunc::GuiOperationText(TW_RESTORE_TEXT, backup_display_name_,
                           gui_parse_text("{@restoring_hdr}"));
  gui_msg(Msg("restoring=Restoring {1}...")(backup_display_name_));

  const std::string full_filename = fs::path(part_settings->backup_folder) / backup_file_name_;

  if (restore_file_system == "emmc") {
    part_settings->total_restore_size = TWFunc::GetFileSize(full_filename);
    if (!RawReadWrite(part_settings)) return false;
  }

  return true;
}

// is_decrypted_ and TW_IS_DECRYPTED both go true as soon as the metadata layer is
// up, so ask the user list instead. Empty when the device isn't FBE.
bool TWPartition::DataIsLocked() {
  for (const UsersStruct& user : *PartitionManager.GetUsersList()) {
    if (user.user_id == 0) return !user.is_decrypted;
  }
  return false;
}

struct AsyncSizeState {
  std::mutex lock;
  bool running = false;
  bool worker_active = false;
  std::atomic<bool> cancel{false};
  std::condition_variable finished;
  TWPartition* pending_partition = nullptr;
  unsigned long long pending_size = 0;
};

// Keep state alive until detached workers finish.
static AsyncSizeState& GetAsyncSizeState() {
  static auto* state = new AsyncSizeState;
  return *state;
}

// Waits for a detached size worker to notice the cancel flag. The worker
// keeps the state alive, so this cannot race with its teardown.
static void StopAsyncDataSize() {
  AsyncSizeState& state = GetAsyncSizeState();
  std::unique_lock lock(state.lock);
  state.cancel.store(true);
  state.finished.wait(lock, [&] { return !state.worker_active; });
  state.pending_partition = nullptr;
  state.running = false;
}

void TWPartition::UpdateDataSizeAsync() {
  AsyncSizeState& state = GetAsyncSizeState();
  {
    std::lock_guard lock(state.lock);
    if (!backup_size_provisional_ || state.running) return;
    state.running = true;
    state.worker_active = true;
    state.cancel.store(false);
  }

  // The worker must use a snapshot; BackupTar() mutates the real list.
  TWExclude exclusions = backup_exclusions;
  std::string path = mount_point_;

  if (IsMounted()) {
    auto partition = this;
    std::string dev = decrypted_block_device_.empty() ? actual_block_device_ : decrypted_block_device_;
    std::thread([partition, exclusions, path, dev]() mutable {
      unsigned long long size = 0;
      // Prefer filesystem counters, then fall back to the directory walk.
      const std::atomic<bool>* cancel = &GetAsyncSizeState().cancel;
      const uint64_t es = exclusions.Get_Exclusions_Folder_Size(cancel);
      if (!cancel->load() && es > 0 && !dev.empty()) {
        char cmdBuf[256];
        constexpr char _cmd[] =
            "dump.f2fs -d1 %s | grep %s | awk -F ': ' '{print $2}' | awk -F ']' '{print $1}'";
        snprintf(cmdBuf, sizeof(cmdBuf), _cmd, dev.c_str(), "valid_block_count");
        if (std::string result; TWFunc::ExecCmd(cmdBuf, result, false) == 0) {
          if (const uint64_t USCount = ParseOrZero<uint64_t>(result); USCount > 0 && USCount <= 354674688ULL) {
            if (uint64_t used = USCount * 4096; static_cast<int64_t>(used - es) > 0) {
              snprintf(cmdBuf, sizeof(cmdBuf), _cmd, dev.c_str(), "valid_inode_count");
              result.clear();
              if (TWFunc::ExecCmd(cmdBuf, result, false) == 0) {
                if (const uint64_t UICount = ParseOrZero<uint64_t>(result);
                  UICount > 0 && used > UICount * 4096) used -= UICount * 4096;
              }
              if (static_cast<int64_t>(used - es) > 0) size = used - es;
            }
          }
        }
        LOGINFO("Data size: f2fs fast path %s (exclusions %lu bytes).\n", size > 0 ? "hit" : "miss", es);
      }
      if (size == 0 && !cancel->load()) {
        // dump.f2fs unavailable: use the old walk.
        LOGINFO("Data size: falling back to folder walk.\n");
        size = exclusions.Get_Folder_Size(path, false);
      }
      AsyncSizeState& state = GetAsyncSizeState();
      std::lock_guard lock(state.lock);
      // A cancelled walk must not publish a partial size or hold the mount.
      if (!cancel->load()) {
        state.pending_partition = partition;
        state.pending_size = size;
      }
      state.worker_active = false;
      state.finished.notify_all();
    }).detach();
  } else {
    LOGINFO("'%s' is not mounted, keeping the statfs backup size.\n", path.c_str());
    std::lock_guard lock(state.lock);
    state.running = false;
    state.worker_active = false;
    state.finished.notify_all();
  }
}

void TWPartition::ApplyAsyncDataSize() {
  AsyncSizeState& state = GetAsyncSizeState();
  uint64_t size;
  {
    std::lock_guard lock(state.lock);
    if (state.pending_partition != this) return;
    size = state.pending_size;
    state.pending_partition = nullptr;
    state.running = false;
  }
  used_ = size;
  backup_size_ = size;
  backup_size_provisional_ = false;
  DataManager::SetValue(TW_BACKUP_DATA_SIZE, size);
  LOGINFO("Data backup size is %s.\n", UnitConversion::FormatBytes(size).c_str());
}

bool TWPartition::UpdateSize(const bool display_error, const bool defer_folder_size) {
  bool ret = false, was_already_mounted = false, ro = false;

  FindActualBlockDevice();

  if (actual_block_device_.empty()) return false;

  ro = mount_read_only_;
  mount_read_only_ = true;
  auto restore_ro = android::base::make_scope_guard([&]() {
    mount_read_only_ = ro;
  });

  if (!can_be_mounted_ && !is_encrypted_) {
    if (TWFunc::IsPathExists(actual_block_device_) && FindPartitionSize()) {
      used_ = size_;
      backup_size_ = size_;
      return true;
    }
    return false;
  }

  was_already_mounted = IsMounted();

  if (removable_ || is_encrypted_) {
    if (!Mount(false)) return true;
  } else if (!Mount(display_error)) return false;

  ret = GetSizeViaStatfs(display_error);
  if (!ret || size_ == 0) {
    if (!GetSizeViaDf(display_error)) {
      if (!was_already_mounted) Unmount(false);
      return false;
    }
  }

  if (has_data_media_) {
    if (Mount(display_error)) {
      if (defer_folder_size) {
        // Leave the statfs figure in place for now. Whoever deferred
        // decides whether the walk is worth starting.
        backup_size_provisional_ = true;
        LOGINFO("Deferring the data backup size to a background walk.\n");
      } else {
        used_ = backup_exclusions.Get_Folder_Size(mount_point_);
        backup_size_ = used_;
        backup_size_provisional_ = false;
        LOGINFO("Data backup size is %s, free: %s.\n", UnitConversion::FormatBytes(used_).c_str(),
                UnitConversion::FormatBytes(free_).c_str());
      }
    } else {
      if (!was_already_mounted) Unmount(false);
      return false;
    }
  } else if (has_android_secure_) {
    if (Mount(display_error)) backup_size_ = backup_exclusions.Get_Folder_Size(backup_path_);
    else {
      if (!was_already_mounted) Unmount(false);
      return false;
    }
  }
  if (!was_already_mounted) Unmount(false);
  return true;
}

// Whatever the volume calls itself, empty when it has no label.
static std::string GetVolumeLabel(const std::string& block_device) {
  const char* label;
  std::string name;

  const auto pr = blkid_new_probe_from_filename(block_device.c_str());
  if (!pr) return name;
  // Free the probe on every exit path from here.
  auto pr_guard = android::base::make_scope_guard([&] {
    blkid_free_probe(pr);
  });
  if (blkid_do_fullprobe(pr) == 0 && blkid_probe_lookup_value(pr, "LABEL", &label, nullptr) ==
      0) name = label;
  return name;
}

bool TWPartition::FindWildcardBlockDevices(const std::string& device) {
  // we will need to create separate mount points for each partition found and we use this index to name each one
  int mount_point_index = 0;
  const std::string path = TWFunc::GetPath(device);
  std::string Dev = fs::path(device).filename().string();
  size_t wildcard_index = Dev.find('*');
  if (wildcard_index != std::string::npos) Dev = Dev.substr(0, wildcard_index);
  wildcard_index = Dev.size();
  DIR* d = opendir(path.c_str());
  if (!d) {
    LOGINFO("Error opening '%s': %s\n", path.c_str(), strerror(errno));
    return false;
  }
  struct dirent* de;
  while ((de = readdir(d))) {
    std::string_view name(de->d_name);
    if (de->d_type != DT_BLK || name.size() <= wildcard_index || name.substr(0, wildcard_index) !=
        Dev) continue;

    // GetPath() keeps the trailing slash, so do not add another one.
    std::string item = path + de->d_name;
    if (PartitionManager.FindPartitionByBlockDevice(item)) continue;
    auto* part = new TWPartition;
    std::string buffer = std::format("{} {}-{} auto defaults defaults", item, mount_point_,
                                     ++mount_point_index);
    part->ProcessFstabLine(buffer.c_str(), false, nullptr);
    // Prefer the label, number whatever name is already taken.
    std::string display = GetVolumeLabel(item);
    if (display.empty()) display = storage_name_;
    if (PartitionManager.
      StorageNameInUse(display)) display += " " + std::to_string(mount_point_index);
    part->storage_name_ = display;
    part->display_name_ = display;
    part->primary_block_device_ = item;
    part->wildcard_block_device_ = false;
    part->is_sub_partition_ = true;
    part->sub_partition_of_ = mount_point_;
    part->is_storage_ = is_storage_;
    part->can_be_mounted_ = true;
    part->removable_ = true;
    part->can_be_wiped_ = can_be_wiped_;
    part->wipe_available_in_gui_ = wipe_available_in_gui_;
    part->FindActualBlockDevice();
    part->UpdateSize(false);
    has_sub_partition_ = true;
    TWPartitionManager::OutputPartition(part);
    PartitionManager.AddPartition(part);
  }
  closedir(d);
  return mount_point_index > 0;
}

void TWPartition::FindActualBlockDevice() {
  if (!sysfs_entry_.empty() && primary_block_device_.empty() && decrypted_block_device_.empty()) {
    /* sysfs_entry_.empty() indicates if this is a sysfs entry that begins with /device/
     * If we have a syfs entry then we are looking for this device from a uevent add.
     * The uevent add will set the primary block device based on the data we receive from
     * after checking for adopted storage. If the device ends up being adopted, then the
     * decrypted block device will be set instead of the primary block device. */
    is_present_ = false;
    return;
  }
  if (wildcard_block_device_ && !is_adopted_storage_) {
    is_present_ = false;
    actual_block_device_ = "";
    can_be_mounted_ = false;
    if (!FindWildcardBlockDevices(primary_block_device_)) {
      if (const auto device = primary_block_device_.substr(0, primary_block_device_.find('*'));
        TWFunc::IsPathExists(device)) {
        is_present_ = true;
        can_be_mounted_ = true;
        actual_block_device_ = device;
      }
    }
    return;
  }
  if (is_decrypted_ && !decrypted_block_device_.empty()) {
    actual_block_device_ = decrypted_block_device_;
    if (TWFunc::IsPathExists(decrypted_block_device_)) {
      is_present_ = true;
      return;
    }
  } else if (slot_select_ && TWFunc::IsPathExists(
                 original_block_device_ + PartitionManager.GetActiveSlotSuffix())) {
    actual_block_device_ = original_block_device_ + PartitionManager.GetActiveSlotSuffix();
    unlink(original_block_device_.c_str());
    symlink(actual_block_device_.c_str(), original_block_device_.c_str());
    // we create a non-slot symlink pointing to the currently selected slot which may assist zips with installing
    is_present_ = true;
    return;
  } else if (TWFunc::IsPathExists(primary_block_device_)) {
    is_present_ = true;
    actual_block_device_ = primary_block_device_;
    return;
  }
  if (!alternate_block_device_.empty() && TWFunc::IsPathExists(alternate_block_device_)) {
    actual_block_device_ = alternate_block_device_;
    is_present_ = true;
  } else {
    is_present_ = false;
  }
}

void TWPartition::RecreateMediaFolder() {
  const std::string media_path = mount_point_ + "/media";

  if (is_fbe_) {
    LOGINFO("Not recreating media folder on FBE\n");
    return;
  }
  if (!Mount(true)) {
    gui_msg(Msg(msg::kError, "recreate_folder_err=Unable to recreate {1} folder.")(media_path));
  } else if (!TWFunc::IsPathExists(media_path)) {
    PartitionManager.MountByPath(symlink_mount_point_, true);
    LOGINFO("Recreating %s folder.\n", media_path.c_str());
    mkdir(media_path.c_str(), 0770);
    const std::string internal_path = DataManager::GetStrValue("tw_internal_path");
    if (!internal_path.empty()) {
      LOGINFO("Recreating %s folder.\n", internal_path.c_str());
      mkdir(internal_path.c_str(), 0770);
    }
#ifdef TW_INTERNAL_STORAGE_PATH
    mkdir(TW_INTERNAL_STORAGE_PATH, 0770);
#endif

    // Afterwards, we will try to set the
    // default metadata that we were hopefully able to get during
    // early boot.
    tw_set_default_metadata(media_path.c_str());
    if (!internal_path.empty()) tw_set_default_metadata(internal_path.c_str());

    // Toggle mount to ensure that "internal sdcard" gets mounted
    PartitionManager.UnmountByPath(symlink_mount_point_, true);
    PartitionManager.MountByPath(symlink_mount_point_, true);
  }
}

void TWPartition::RecreateAndSecFolder() {
  if (!has_android_secure_) return;
  LOGINFO("Creating %s: %s\n", backup_display_name_.c_str(), symlink_path_.c_str());
  if (!Mount(true)) {
    gui_msg(Msg(msg::kError, "recreate_folder_err=Unable to recreate {1} folder.")(backup_name_));
  } else if (!TWFunc::IsPathExists(symlink_path_)) {
    LOGINFO("Recreating %s folder.\n", backup_name_.c_str());
    PartitionManager.MountByPath(symlink_mount_point_, true);
    mkdir(symlink_path_.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    PartitionManager.UnmountByPath(symlink_mount_point_, true);
  }
}

uint64_t TWPartition::GetMaxFileSize() const {
  uint64_t maxFileSize = 0;
  constexpr uint64_t kGB = 1024 * 1024 * 1024;
  constexpr uint64_t kTB = kGB * 1024;
  constexpr uint64_t kPB = kTB * 1024;
  if (current_file_system_ == "ext4") maxFileSize = 16 * kTB; //16 TB
  else if (current_file_system_ == "vfat") maxFileSize = 4 * kGB; //4 GB
  else if (current_file_system_ == "ntfs" || current_file_system_ == "tntfs")
    maxFileSize = 256 * kTB; //256 TB
  else if (current_file_system_ == "exfat") maxFileSize = 16 * kPB; //16 PB
  else if (current_file_system_ == "ext3") maxFileSize = 2 * kTB; //2 TB
  else if (current_file_system_ == "f2fs" || current_file_system_ == "mifs")
    maxFileSize = static_cast<uint64_t>(3.94 * kTB); //3.94 TB
  else maxFileSize = 100000000L;
  return maxFileSize - 1;
}

bool TWPartition::FlashImage(const PartitionSettings* part_settings) {
  const std::filesystem::path full_filename = std::filesystem::path(part_settings->backup_folder) /
                                        backup_file_name_;

  LOGINFO("Image filename is: %s\n", backup_file_name_.c_str());

  if (backup_method_ == BackupMethod::kFiles) {
    LOGERR("Cannot flash images to file systems\n");
    return false;
  }
  if (!can_flash_img_) {
    LOGERR("Cannot flash images to partitions %s\n", display_name_.c_str());
    return false;
  }
  if (!FindPartitionSize()) {
    LOGERR("Unable to find partition size for '%s'\n", mount_point_.c_str());
    return false;
  }
  if (const uint64_t image_size = TWFunc::GetFileSize(full_filename); image_size > size_) {
    LOGINFO("Size (%lu bytes) of image '%s' is larger than target device '%s' (%lu bytes)\n",
            image_size, backup_file_name_.c_str(), actual_block_device_.c_str(), size_);
    gui_err("img_size_err=Size of image is larger than target device");
    return false;
  }
  if (backup_method_ == BackupMethod::kDd) {
    if (IsSparseImage(full_filename)) {
      return FlashSparseImage(full_filename);
    }
    return RawReadWrite(part_settings);
  }

  LOGERR("Unknown flash method for '%s'\n", mount_point_.c_str());
  return false;
}

bool TWPartition::IsSparseImage(const std::string& Filename) {
  uint32_t magic = 0;
  const android::base::unique_fd fd(open(Filename.c_str(), O_RDONLY));
  if (!fd.ok()) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(
            strerror(errno)));
    return false;
  }

  if (read(fd.get(), &magic, sizeof(magic)) != sizeof(magic)) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(
            strerror(errno)));
    return false;
  }
  return magic == SPARSE_HEADER_MAGIC;
}

bool TWPartition::FlashSparseImage(const std::string& Filename) const {
#ifdef TW_ENABLE_BLKDISCARD
  BlkDiscard();
#endif

  gui_msg(Msg("flashing=Flashing {1}...")(display_name_));

  const android::base::unique_fd in(open(Filename.c_str(), O_RDONLY));
  if (!in.ok()) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(
            strerror(errno)));
    return false;
  }

  struct sparse_file* s = sparse_file_import(in.get(), true, false);
  if (!s) {
    gui_msg(Msg(msg::kError, "sparse_import_err=Failed to import sparse image '{1}'")(Filename));
    return false;
  }

  const android::base::unique_fd out(open(actual_block_device_.c_str(), O_WRONLY));
  if (!out.ok()) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(actual_block_device_)(
            strerror(errno)));
    sparse_file_destroy(s);
    return false;
  }

  const int ret = sparse_file_write(s, out.get(), false, false, false);
  sparse_file_destroy(s);
  if (ret < 0) {
    LOGERR("Failed to flash sparse image '%s' to '%s'\n", Filename.c_str(),
           actual_block_device_.c_str());
    return false;
  }
  return true;
}

void TWPartition::ChangeMountReadOnly(const bool new_value) {
  mount_read_only_ = new_value;
}

bool TWPartition::IsReadOnly() const {
  return mount_read_only_;
}

int TWPartition::CheckLifetimeWrites() {
  const bool original_read_only = mount_read_only_;
  int ret = 1;

  mount_read_only_ = true;
  if (Mount(false)) {
    FindActualBlockDevice();
    std::string temp = actual_block_device_;
    FindRealBlockDevice(temp);
    const std::string block = basename(temp.c_str());
    const std::filesystem::path file = fs::path("/sys/fs") / current_file_system_ / block /
                                 "lifetime_write_kbytes";

    if (std::error_code ec; std::filesystem::exists(file, ec)) {
      std::string result;
      if (TWFunc::ReadFile(file, result) != 0) {
        LOGINFO("CheckLifetimeWrites of '%s' failed to ReadFile\n", file.c_str());
      } else {
        LOGINFO("CheckLifetimeWrites result: '%s'\n", result.c_str());
        if (result == "0") {
          ret = 0;
        }
      }
    } else {
      LOGINFO("CheckLifetimeWrites file does not exist '%s'\n", file.c_str());
    }
    Unmount(true);
  } else {
    LOGINFO("CheckLifetimeWrites failed to mount '%s'\n", mount_point_.c_str());
  }
  mount_read_only_ = original_read_only;
  return ret;
}

void TWPartition::SetBackupFileName(const std::string& filename) {
  backup_file_name_ = filename;
}

std::string TWPartition::GetBackupName() {
  return backup_name_;
}

std::string TWPartition::GetMountPoint() {
  return mount_point_;
}

void TWPartition::SetBlockDevice(const std::string& block_device) {
  primary_block_device_ = actual_block_device_ = block_device;
}

bool TWPartition::GetSuperStatus() const {
  return is_super_;
}

void TWPartition::SetCanBeBackedUp(const bool val) {
  can_be_backed_up_ = val;
}

void TWPartition::SetCanBeWiped(const bool val) {
  can_be_wiped_ = val;
  wipe_available_in_gui_ = val;
}

std::string TWPartition::GetBackupFileName() {
  return backup_file_name_;
}

std::string TWPartition::GetDisplayName() {
  return display_name_;
}

bool TWPartition::IsSlotSelect() const {
  return slot_select_;
}
