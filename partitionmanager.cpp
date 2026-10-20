/*
    Copyright 2014 to 2021 TeamWin
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

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <linux/fs.h>
#include <linux/netlink.h>
#include <linux/types.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include <fs_mgr.h>
#include <fs_mgr_dm_linear.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <fstab/fstab.h>
#include <libavb_user/libavb_user.h>
#include <liblp/builder.h>
#include <libsnapshot/snapshot.h>

#include "data.hpp"
#include "exclude.hpp"
#include "fixContexts.hpp"
#include "fstab/fstab_parser.hpp"
#include "partitions.hpp"
#include "progresstracking.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "twrpRepacker.hpp"
#include "twrp_apex.hpp"
#include "twrp_digest_driver.hpp"
#include "twrp_functions.hpp"
#include "unit_conversion.hpp"
#include "variables.h"
#include "gui/gui.hpp"
#include "startup/startup_args.hpp"

#ifdef TW_LOAD_VENDOR_MODULES
#include "kernel_module_loader.hpp"
#endif

#ifdef TW_HAS_MTP
#include "MtpMessage.hpp"
#include "TwrpMtp.hpp"
#endif

#ifdef TW_INCLUDE_CRYPTO
#include "gui/pages.hpp"
#include "gui/rapidxml.hpp"
#ifdef TW_INCLUDE_FBE
#include "Decrypt.h"
#include "FsCrypt.h"
#ifdef TW_INCLUDE_FBE_METADATA_DECRYPT
#ifdef USE_FSCRYPT
#include "MetadataCrypt.h"
#endif
#endif
#endif
#endif

#ifdef AB_OTA_UPDATER
#include <BootControlClient.h>
using android::hal::BootControlClient;
using android::hal::CommandResult;
#endif

using android::fs_mgr::DestroyLogicalPartition;
using android::fs_mgr::Fstab;
using android::fs_mgr::FstabEntry;
using android::fs_mgr::MetadataBuilder;

extern bool datamedia;

static std::string additional_fstab = "/etc/additional.fstab";

constexpr uint64_t kMiB = 1024 * 1024;

// Member (not free) function so the TWPartition friendship covers the field access
bool TWPartitionManager::MatchesPath(const TWPartition* p, const std::string& path) {
  return p->mount_point_ == path
         || (!p->symlink_mount_point_.empty() && p->symlink_mount_point_ == path);
}

TWPartitionManager::TWPartitionManager() {
  mtp_was_enabled = false;
  mtp_write_fd = -1;
  uevent_pfd.fd = -1;
  stop_backup = false;
#ifdef AB_OTA_UPDATER
  active_slot_display_ = "";
  const std::string slot = android::base::GetProperty("ro.boot.slot_suffix", "");
  SetActiveSlot(slot == "_a" ? "A" : slot == "_b" ? "B" : "");
#endif
}

void TWPartitionManager::SetCryptoState() {
  if (android::base::GetProperty("ro.crypto.state", "error") == "error") {
    android::base::SetProperty("ro.crypto.state", "encrypted");
  }
}

int TWPartitionManager::SetCryptoType(const std::string& crypto_type) {
  if (android::base::GetProperty("ro.crypto.type", "error") == "error") {
    android::base::SetProperty("ro.crypto.type", crypto_type);
    sleep(1);
  }
  return 0;
}

static int OverrideProp(const std::string& prop, const std::string& mountpoint,
                        const std::string& prop_file_name, std::string& prop_value) {
  const std::string partition_prop = TWFunc::GetPropertyFromPartition(
      prop, PartitionManager, mountpoint, prop_file_name);
  if (partition_prop.empty()) return 1;

  if (TWFunc::OverrideProperty(prop, partition_prop) == TWFunc::kNotAvailable) {
    LOGERR("Unable to override '%s' due to missing libresetprop\n", prop.c_str());
  } else {
    prop_value = android::base::GetProperty(prop, "");
    LOGINFO("Setting '%s' to '%s' from %s/%s\n", prop.c_str(), prop_value.c_str(),
            mountpoint.c_str(), prop_file_name.c_str());
  }

  return 0;
}

static void ResetPropFromPartition(const std::string& prop, const std::string& def,
                                   const TWPartition* ven,
                                   const TWPartition* odm) {
  bool prop_on_odm = false, prop_on_vendor = false;
  std::string prop_value;
  if (odm && OverrideProp(prop, "/odm", "etc/build.prop", prop_value) == 0) {
    prop_on_odm = true;
  }
  if (ven && OverrideProp(prop, "/vendor", "build.prop", prop_value) == 0) {
    prop_on_vendor = true;
  }
  if (!prop_on_odm && !prop_on_vendor && !def.empty()) {
    if (TWFunc::OverrideProperty(prop, def) == TWFunc::kNotAvailable) {
      LOGERR("Unable to override '%s' due to missing libresetprop\n", prop.c_str());
    } else {
      prop_value = android::base::GetProperty(prop, "");
      LOGINFO("Setting '%s' to default value (%s)\n", prop.c_str(), prop_value.c_str());
    }
  }
  prop_value = android::base::GetProperty(prop, "");
  if (!prop_on_odm && !prop_on_vendor && !prop_value.empty() && def.empty()) {
    if (TWFunc::DeleteProperty(prop) == TWFunc::kNotAvailable) {
      LOGERR("Unable to delete '%s' due to missing libresetprop\n", prop.c_str());
    } else {
      LOGINFO("Deleting property '%s'\n", prop.c_str());
    }
  }
}

static bool DoDisableAvb2(const std::string& ab_suffix, const bool disable_verity,
                          const bool disable_verification, const bool display_info) {
  std::string vbmeta_part = "vbmeta";
  if (!ab_suffix.empty()) vbmeta_part += ab_suffix;
  AvbOps* ops = avb_ops_user_new();
  if (!ops) {
    if (display_info)
      gui_msg(
          Msg(msg::kError, "disable_avb2_fail_msg=Disable AVB2.0: processing '{1}' failed!")(
              vbmeta_part));
    return false;
  }
  bool ok = true;
  if (disable_verity) {
    if (!avb_user_verity_set(ops, ab_suffix.c_str(), false)) {
      ok = false;
      if (display_info)
        gui_msg(Msg(msg::kError,
                    "disable_avb2_fail_msg=Disable AVB2.0: processing '{1}' failed!")(vbmeta_part));
    }
  }
  if (disable_verification) {
    if (!avb_user_verification_set(ops, ab_suffix.c_str(), false)) {
      ok = false;
      if (display_info)
        gui_msg(Msg(msg::kError,
                    "disable_avb2_fail_msg=Disable AVB2.0: processing '{1}' failed!")(vbmeta_part));
    }
  }
  if (ok && display_info) {
    gui_msg(
        Msg(msg::kHighlight,
            "disable_avb2_success_msg=Disable AVB2.0: processing '{1}' successfully.")(
            vbmeta_part));
  }
  avb_ops_user_free(ops);
  return ok;
}

bool TWPartitionManager::DisableAvb2(const bool display_info) {
  constexpr bool disable_verity = true;
#ifdef TW_AVB_VBMETA_FLAGS_ALL_DISABLED
  constexpr bool disable_verification = true;
#else
  constexpr bool disable_verification = false;
#endif
#ifdef AB_OTA_UPDATER
  const bool ok_a = DoDisableAvb2("_a", disable_verity, disable_verification, display_info);
  const bool ok_b = DoDisableAvb2("_b", disable_verity, disable_verification, display_info);
  return ok_a && ok_b;
#else
  return DoDisableAvb2("", disable_verity, disable_verification, display_info);
#endif
}

static void ProcessResetProps(const TWPartition* ven, const TWPartition* odm) {
  // Reset the crypto volume props according to os.
  ResetPropFromPartition("ro.crypto.dm_default_key.options_format.version", "", ven, odm);
  ResetPropFromPartition("ro.crypto.volume.metadata.method", "", ven, odm);
  ResetPropFromPartition("ro.crypto.volume.options", "", ven, odm);
  ResetPropFromPartition("external_storage.casefold.enabled", "", ven, odm);
  ResetPropFromPartition("external_storage.sdcardfs.enabled", "", ven, odm);
}

static std::string KmVerFromManifest(std::string ver) {
  TWFunc::GetServiceFromManifest("/vendor", "android.hardware.keymaster", ver);
  if (ver.find('4') != std::string::npos) {
    ver = "4.x";
  }
  return ver;
}

static void ProcessKeymasterVersion(const TWPartition* ven, const bool display_error) {
  // Fetch the Keymaster Service version to be started
  std::string version;
#ifndef TW_FORCE_KEYMASTER_VER
  version = KmVerFromManifest(version);

  /* If we are unable to get the version from device vendor then
      * set the version from the keymaster_ver prop if set
      */
  if (version.empty()) {
    // unmount partition(s)
    if (ven) ven->Unmount(display_error);

    // Use keymaster_ver prop set from device tree (if exists)
    version = android::base::GetProperty(TW_KEYMASTER_VERSION_PROP, version);
    if (version.empty()) {
      LOGINFO(
          "Keymaster_Ver::Unable to find vendor manifest on the device, and no default value set. Checking the ramdisk manifest\n");
      version = KmVerFromManifest(version);
    } else {
      LOGINFO(
          "Keymaster_Ver::Unable to find vendor manifest on the device. Setting to default value.\n");
    }
  } else {
    if (ven) ven->Unmount(display_error);
  }
#else
  if (ven) ven->Unmount(display_error);

  version = android::base::GetProperty(TW_KEYMASTER_VERSION_PROP, version);
  if (version.empty()) {
    LOGINFO("Keymaster_Ver::Force Keymaster_Ver flag found, but keymaster_ver prop not set.\n");
  } else {
    LOGINFO("Keymaster_Ver::Force Keymaster_Ver flag found.\n");
  }
#endif
  LOGINFO("Keymaster_Ver::Using keymaster version '%s' for decryption\n", version.c_str());
  android::base::SetProperty(TW_KEYMASTER_VERSION_PROP, version);
}

// Parses /etc/twrp.flags into a mount-point-keyed FlagsMap.
static std::map<std::string, FlagsMap> LoadTwrpFlags() {
  std::map<std::string, FlagsMap> twrp_flags;
  const auto result = twrp::fstab::ParseTwrpFlagsFromFile("/etc/twrp.flags");
  if (!result.Ok()) return twrp_flags;
  LOGINFO("Reading /etc/twrp.flags\n");
  for (const auto& entry : result.fstab) {
    FlagsMap lf;
    lf.file_system = entry.fs_type;
    lf.primary_block_device = entry.blk_device;
    lf.alternate_block_device = entry.blk_device2;
    lf.fstab_line = entry.mount_point + ' ' + entry.fs_type + ' ' + entry.blk_device;
    if (!entry.blk_device2.empty()) lf.fstab_line += ' ' + entry.blk_device2;
    if (entry.length != 0) lf.fstab_line += " length=" + std::to_string(entry.length);
    if (!entry.twrp_flags.raw_flags.empty()) {
      lf.flags = "flags=" + entry.twrp_flags.raw_flags;
      lf.fstab_line += " flags=" + entry.twrp_flags.raw_flags;
    }
    twrp_flags[entry.mount_point] = std::move(lf);
  }
  return twrp_flags;
}



#ifndef TW_SKIP_ADDITIONAL_FSTAB
// Copies the vendor fstab to /etc/additional.fstab and switches fstab_filename
// to it; the return value asks the caller for one more, vendor fstab pass.
static bool FetchAdditionalFstab(std::string* fstab_filename) {
  if (!TWFunc::FindFstab(*fstab_filename)) {
    LOGINFO("Unable to parse vendor fstab\n");
    return false;
  }
  LOGINFO("Fstab: %s\n", fstab_filename->c_str());
  TWFunc::CopyFile(*fstab_filename, additional_fstab, 0600, false);
  *fstab_filename = additional_fstab;
  android::base::SetProperty("fstab.additional", "1");
  return true;
}
#endif

bool TWPartitionManager::ProcessFstabFile(const std::string& fstab_filename,
                                          const bool parse_userdata, const bool display_error,
                                          std::map<std::string, FlagsMap>& twrp_flags) {
  // Existence probe; the parser re-opens and reads the file.
  if (!parse_userdata && !std::ifstream(fstab_filename)) {
    LOGERR("Critical Error: Unable to open fstab at '%s'.\n", fstab_filename.c_str());
    return false;
  }
  LOGINFO("Reading %s\n", fstab_filename.c_str());
  const auto result = twrp::fstab::ParseFstabFromFile(fstab_filename);

  // The userdata pass replaces /data and /metadata wholesale.
  const auto erase_partition = [this](const std::string& mount_point) {
    for (auto it = partitions_.begin(); it != partitions_.end();) {
      if ((*it)->mount_point_ == mount_point) {
        delete *it;
        it = partitions_.erase(it);
      } else {
        ++it;
      }
    }
  };

  for (const auto& entry : result.fstab) {
    if (entry.fs_type == "swap") continue; // Skip swap in recovery
    if (parse_userdata) {
      if (entry.mount_point == "/metadata") {
        erase_partition("/metadata");
      } else if (entry.mount_point == "/data") {
        erase_partition("/data");
      } else {
        continue;
      }
    }

    // Rebuild the fstab2 line with the flag columns verbatim so
    // ProcessFstabLine sees the original tokens.
    const std::string line = entry.blk_device + ' ' + entry.mount_point + ' ' +
        entry.fs_type + ' ' + entry.raw_mnt_flags + ' ' + entry.raw_fsmgr_flags;
    auto partition = std::make_unique<TWPartition>();
    bool keep = partition->ProcessFstabLine(line.c_str(), display_error,
                                            parse_userdata ? nullptr : &twrp_flags);
    if (keep && partition->is_super_ && !PrepareSuperVolume(partition.get())) keep = false;
    if (keep) partitions_.push_back(partition.release());
  }
  return true;
}

void TWPartitionManager::AddRemainingTwrpFlags(const std::map<std::string, FlagsMap>& twrp_flags,
                                               const bool display_error) {
  if (twrp_flags.empty()) return;
  LOGINFO("Processing remaining twrp.flags\n");
  // Add any items from twrp.flags that did not exist in the recovery.fstab
  for (const auto& [mount_point, line_flags] : twrp_flags) {
    if (FindPartitionByPath(mount_point) != nullptr) continue;
    // The unique_ptr frees the partition when the fstab line is rejected
    if (auto partition = std::make_unique<TWPartition>();
      partition->ProcessFstabLine(line_flags.fstab_line.c_str(), display_error, nullptr))
      partitions_.push_back(partition.release());
  }
}

int TWPartitionManager::ProcessFstab(std::string fstab_filename, const bool display_error,
                                     const bool recovery_mode) {
  // Without the super device the logical partitions cannot be mapped, so every
  // super entry would fail its DM probe in PrepareSuperVolume.
  const std::string super_partition = GetSuperPartition();
  if (access(super_partition.c_str(), F_OK) == 0) {
    SetupSuperDevices();
  } else {
    LOGINFO("Super partition not found at %s\n", super_partition.c_str());
  }

  auto twrp_flags = LoadTwrpFlags();

  if (!ProcessFstabFile(fstab_filename, false, display_error, twrp_flags)) return false;
  AddRemainingTwrpFlags(twrp_flags, display_error);

  TWPartition* ven = FindPartitionByPath("/vendor");
  TWPartition* odm = FindPartitionByPath("/odm");

  if (recovery_mode) {
    if (ven) ven->Mount(display_error);
    if (odm) odm->Mount(display_error);

    ProcessResetProps(ven, odm);

#ifdef TW_SKIP_ADDITIONAL_FSTAB
    LOGINFO("Skipping Additional Fstab Processing\n");
    android::base::SetProperty("fstab.additional", "0");
#else
    // Vendor fstab pass: replaces the /data and /metadata entries.
    if (FetchAdditionalFstab(&fstab_filename)) {
      if (!ProcessFstabFile(fstab_filename, true, display_error, twrp_flags)) return false;
      ven = FindPartitionByPath("/vendor");
      odm = FindPartitionByPath("/odm");
    }
#endif

    ProcessKeymasterVersion(ven, false);
  }
  LOGINFO("Done processing fstab files\n");

#ifdef TW_LOAD_VENDOR_MODULES
  KernelModuleLoader::LoadVendorModules();
#endif

  if (odm) odm->Unmount(display_error);
  if (ven) ven->Unmount(display_error);

  return true;
}

void TWPartitionManager::SetupFstabPartitions(const bool display_error) {
  TWPartition* settings_partition = nullptr;
  TWPartition* andsec_partition = nullptr;
  unsigned int storageid = 1 << 16;
  // upper 16 bits are for physical storage device, we pretend to have only one

  for (TWPartition* partition : partitions_) {
    partition->PartitionPostProcessing(display_error);

    if (partition->is_storage_) {
      ++storageid;
      partition->mtp_storage_id_ = storageid;
    }

    if (!settings_partition && partition->is_settings_storage_ && partition->
        is_present_)
      settings_partition = partition;
    else partition->is_settings_storage_ = false;

    if (!andsec_partition && partition->has_android_secure_ && partition->
        is_present_)
      andsec_partition = partition;
    else partition->has_android_secure_ = false;
  }

  UnlockBlockPartitions();

  //Setup Apex before decryption
  TWPartition* sys = FindPartitionByPath(GetAndroidRootPath());
  TWPartition* ven = FindPartitionByPath("/vendor");
  if (sys) {
    if (sys->GetSuperStatus()) {
      sys->Mount(display_error);
      if (ven) {
        ven->Mount(display_error);
      }
#ifdef TW_EXCLUDE_APEX
      LOGINFO("Apex is disabled in this build\n");
#else
      if (!TwrpApex::LoadApexImages()) {
        LOGERR("Unable to load apex images from %s\n", TwrpApex::kApexDir.c_str());
        android::base::SetProperty("twrp.apex.loaded", "false");
      } else {
        android::base::SetProperty("twrp.apex.loaded", "true");
      }
      TWFunc::CheckAndRunScript("/sbin/resyncapex.sh", "apex");
#endif
    }
  }
#ifndef USE_VENDOR_LIBS
  if (ven) ven->Unmount(display_error);
  if (sys) sys->Unmount(display_error);
#endif

  if (!datamedia && !settings_partition && !FindPartitionByPath("/sdcard") &&
      !FindPartitionByPath("/internal_sd") &&
      !FindPartitionByPath("/internal_sdcard") &&
      !FindPartitionByPath("/emmc")) {
    // Attempt to automatically identify /data/media emulated storage devices
    if (TWPartition* data = FindPartitionByPath("/data")) {
      LOGINFO("Using automatic handling for /data/media emulated storage device.\n");
      datamedia = true;
      data->SetupDataMedia();
      settings_partition = data;
      // Since /data was not considered a storage partition earlier, we still need to assign an MTP ID
      ++storageid;
      data->mtp_storage_id_ = storageid;
    }
  }
  if (!settings_partition) {
    for (TWPartition* partition : partitions_) {
      if (partition->is_storage_) {
        settings_partition = partition;
        break;
      }
    }
    if (!settings_partition)
      LOGERR("Unable to locate storage partition for storing settings file.\n");
  }
  if (!WriteFstab()) {
    if (display_error)
      LOGERR("Error creating fstab\n");
    else
      LOGINFO("Error creating fstab\n");
  }

  if (andsec_partition) {
    SetupAndroidSecureLocation(andsec_partition);
  } else if (settings_partition) {
    SetupAndroidSecureLocation(settings_partition);
  }
  if (settings_partition) {
    SetupSettingsStoragePartition(settings_partition);
  }

#ifdef TW_INCLUDE_CRYPTO
  DataManager::SetValue(TW_IS_ENCRYPTED, 1);
  DecryptData();
#endif

  UpdateSystemDetails(true);
  if (GetSuperStatus()) SetupSuperPartition();
  UnmountMainPartitions();
#ifdef AB_OTA_UPDATER
  DataManager::SetValue("tw_active_slot", GetActiveSlotDisplay());
#endif
  SetupUevent();
}

int TWPartitionManager::WriteFstab() const {
  std::ofstream fp("/etc/fstab");
  if (!fp) {
    LOGINFO("Can not open /etc/fstab.\n");
    return false;
  }
  for (const TWPartition* partition : partitions_) {
    if (partition->can_be_mounted_) {
      fp << partition->actual_block_device_ << ' ' << partition->mount_point_ << ' '
          << partition->current_file_system_ << (partition->mount_read_only_ ? " ro " : " rw ")
          << "0 0\n";
    }
    // Handle subpartition tracking
    if (partition->is_sub_partition_) {
      if (TWPartition* parent_partition = FindPartitionByPath(partition->sub_partition_of_)) {
        parent_partition->has_sub_partition_ = true;
      } else {
        LOGERR("Unable to locate parent partition '%s' of '%s'\n",
               partition->sub_partition_of_.c_str(), partition->mount_point_.c_str());
      }
    }
  }
  return true;
}

void TWPartitionManager::DecryptData() {
#ifdef TW_INCLUDE_CRYPTO
  if (TWPartition* data = FindPartitionByPath("/data");
    data && data->is_encrypted_ && !data->is_decrypted_) {
    SetCryptoState();
    // Ensure the metadata key directory's partition is mounted before we use it.
    if (const TWPartition* key_dir = FindPartitionByPath(data->key_directory_);
      key_dir && !key_dir->IsMounted())
      MountByPath(data->key_directory_, false);
    if (!data->key_directory_.empty()) {
      SetCryptoType("file");
#ifdef TW_INCLUDE_FBE_METADATA_DECRYPT
#ifdef USE_FSCRYPT
      // Unmounting /data keeps the metadata mapping alive, and rebuilding it would
      // collide with its own device-mapper name, so reuse the mapping when it is
      // still there and only put the DE keys back.
      std::string crypto_blkdev = data->decrypted_block_device_;
      if (!crypto_blkdev.empty() && !TWFunc::IsPathExists(crypto_blkdev))
        crypto_blkdev.clear();
      if (crypto_blkdev.empty())
        crypto_blkdev =
            android::base::GetProperty("ro.crypto.fs_crypto_blkdev", "error");
      const bool reused_mapping =
          crypto_blkdev != "error" && TWFunc::IsPathExists(crypto_blkdev);
      constexpr std::vector<bool> device_aliased;
      if (constexpr std::vector<std::string> user_devices;
        reused_mapping ||
        android::vold::fscrypt_mount_metadata_encrypted(data->actual_block_device_,
                                                        data->mount_point_, false, false,
                                                        data->current_file_system_, false,
                                                        user_devices, device_aliased, 0,
                                                        TWFunc::IsPathExists(additional_fstab)
                                                          ? additional_fstab
                                                          : "")) {
        if (!reused_mapping)
          crypto_blkdev =
              android::base::GetProperty("ro.crypto.fs_crypto_blkdev", "error");
        data->decrypted_block_device_ = crypto_blkdev;
        LOGINFO(
            "Successfully decrypted metadata encrypted data partition with new block device: '%s'\n",
            crypto_blkdev.c_str());
#endif
        data->is_decrypted_ = true; // Needed to make the mount function work correctly
        int retry_count = 10;
        while (!data->Mount(false) && --retry_count) usleep(500);
        if (data->Mount(false)) {
          if (!data->DecryptFbeDe())
            LOGERR("Unable to decrypt FBE device\n");
        } else {
          LOGINFO("Failed to mount data after metadata decrypt\n");
        }
      } else {
        LOGINFO("Unable to decrypt metadata encryption\n");
      }
#else
      LOGERR("Metadata FBE decrypt support not present in this TWRP\n");
#endif
    }
    if (data->is_fbe_) {
      if (DataManager::GetIntValue(TW_CRYPTO_PWTYPE) == 0) {
        if (DecryptDevice("!") == 0) {
          gui_msg("decrypt_success=Successfully decrypted with default password.");
          DataManager::SetValue(TW_IS_ENCRYPTED, 0);
        } else {
          gui_err("unable_to_decrypt=Unable to decrypt with default password.");
        }
      }
    }
  }
#endif
}

void TWPartitionManager::SetupSettingsStoragePartition(const TWPartition* part) {
  DataManager::SetValue("tw_storage_path", part->storage_path_);
}

void TWPartitionManager::SetupAndroidSecureLocation(TWPartition* part) {
  if (part->has_android_secure_ || !datamedia) {
    part->SetupAndSec();
  }
}

void TWPartitionManager::OutputPartitionLogging() const {
  printf("\n\nPartition Logs:\n");
  for (const TWPartition* partition : partitions_) OutputPartition(partition);
}

void TWPartitionManager::OutputPartition(const TWPartition* part) {
  printf("%s | %s | Size: %s", part->mount_point_.c_str(), part->actual_block_device_.c_str(),
         UnitConversion::FormatBytes(part->size_).c_str());
  if (part->can_be_mounted_) {
    printf(" Used: %s Free: %s Backup Size: %s", UnitConversion::FormatBytes(part->used_).c_str(),
           UnitConversion::FormatBytes(part->free_).c_str(),
           UnitConversion::FormatBytes(part->backup_size_).c_str());
  }
  struct FlagEntry {
    bool TWPartition::* flag;
    const char* name;
  };
  static constexpr FlagEntry kFlags[] = {
    { &TWPartition::can_be_mounted_, "Can_Be_Mounted" },
    { &TWPartition::can_be_wiped_, "Can_Be_Wiped" },
    { &TWPartition::use_rm_rf_, "Use_Rm_Rf" },
    { &TWPartition::can_be_backed_up_, "Can_Be_Backed_Up" },
    { &TWPartition::wipe_during_factory_reset_, "Wipe_During_Factory_Reset" },
    { &TWPartition::wipe_available_in_gui_, "Wipe_Available_in_GUI" },
    { &TWPartition::is_sub_partition_, "Is_SubPartition" },
    { &TWPartition::has_sub_partition_, "Has_SubPartition" },
    { &TWPartition::removable_, "Removable" },
    { &TWPartition::is_present_, "IsPresent" },
    { &TWPartition::can_be_encrypted_, "Can_Be_Encrypted" },
    { &TWPartition::is_encrypted_, "Is_Encrypted" },
    { &TWPartition::is_decrypted_, "Is_Decrypted" },
    { &TWPartition::has_data_media_, "Has_Data_Media" },
    { &TWPartition::can_encrypt_backup_, "Can_Encrypt_Backup" },
    { &TWPartition::use_userdata_encryption_, "Use_Userdata_Encryption" },
    { &TWPartition::has_android_secure_, "Has_Android_Secure" },
    { &TWPartition::is_storage_, "Is_Storage" },
    { &TWPartition::is_settings_storage_, "Is_Settings_Storage" },
    { &TWPartition::ignore_blkid_, "Ignore_Blkid" },
    { &TWPartition::mount_to_decrypt_, "Mount_To_Decrypt" },
    { &TWPartition::can_flash_img_, "Can_Flash_Img" },
    { &TWPartition::is_adopted_storage_, "Is_Adopted_Storage" },
    { &TWPartition::slot_select_, "SlotSelect" },
    { &TWPartition::mount_read_only_, "Mount_Read_Only" },
    { &TWPartition::is_super_, "Is_Super" },
  };
  std::string flags;
  for (const auto& [f, n] : kFlags)
    if (part->*f) {
      flags += n;
      flags += ' ';
    }
  printf("\n   Flags: %s\n", flags.c_str());
  if (!part->sub_partition_of_.empty())
    printf("   SubPartition_Of: %s\n",
           part->sub_partition_of_.c_str());
  if (!part->symlink_path_.empty()) printf("   Symlink_Path: %s\n", part->symlink_path_.c_str());
  if (!part->symlink_mount_point_.empty())
    printf("   Symlink_Mount_Point: %s\n",
           part->symlink_mount_point_.c_str());
  if (!part->primary_block_device_.empty())
    printf("   Primary_Block_Device: %s\n",
           part->primary_block_device_.c_str());
  if (!part->alternate_block_device_.empty())
    printf("   Alternate_Block_Device: %s\n",
           part->alternate_block_device_.c_str());
  if (!part->decrypted_block_device_.empty())
    printf("   Decrypted_Block_Device: %s\n",
           part->decrypted_block_device_.c_str());
  if (part->length_ != 0) printf("   Length: %i\n", part->length_);
  if (!part->display_name_.empty()) printf("   Display_Name: %s\n", part->display_name_.c_str());
  if (!part->storage_name_.empty()) printf("   Storage_Name: %s\n", part->storage_name_.c_str());
  if (!part->backup_path_.empty()) printf("   Backup_Path: %s\n", part->backup_path_.c_str());
  if (!part->backup_name_.empty()) printf("   Backup_Name: %s\n", part->backup_name_.c_str());
  if (!part->backup_display_name_.empty())
    printf("   Backup_Display_Name: %s\n",
           part->backup_display_name_.c_str());
  if (!part->backup_file_name_.empty())
    printf("   Backup_FileName: %s\n",
           part->backup_file_name_.c_str());
  if (!part->storage_path_.empty()) printf("   Storage_Path: %s\n", part->storage_path_.c_str());
  if (!part->current_file_system_.empty())
    printf("   Current_File_System: %s\n",
           part->current_file_system_.c_str());
  if (!part->fstab_file_system_.empty())
    printf("   Fstab_File_System: %s\n",
           part->fstab_file_system_.c_str());
  if (part->format_block_size_ !=
      0)
    printf("   Format_Block_Size: %lu\n", part->format_block_size_);
  printf("   Backup_Method: %s\n", part->BackupMethodByName().c_str());
  if (part->mount_flags_ || !part->mount_options_.empty())
    printf(
        "   Mount_Flags: %i, Mount_Options: %s\n", part->mount_flags_,
        part->mount_options_.c_str());
  if (part->mtp_storage_id_) printf("   MTP_Storage_ID: %i\n", part->mtp_storage_id_);
  if (!part->key_directory_.empty())
    printf("   Metadata Key Directory: %s\n",
           part->key_directory_.c_str());
  printf("\n");
}

int TWPartitionManager::MountByPath(const std::string& Path, const bool display_error) const {
  int ret = false;
  bool found = false;
  const std::string local_path = TWFunc::GetRootPath(Path);

  if (local_path == "/tmp" || local_path == "/") return true;

  // Iterate through all partitions
  for (TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) {
      ret = partition->Mount(display_error);
      found = true;
    } else if (partition->is_sub_partition_ && partition->sub_partition_of_ == local_path) {
      partition->Mount(display_error);
    }
  }
  if (found) {
    return ret;
  }
  if (display_error) {
    gui_msg(
        Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(
            local_path));
  }
  return false;
}

bool TWPartitionManager::UnmountByPath(const std::string& path, const bool display_error,
                                       const int flags) const {
  int ret = false;
  bool found = false;
  const std::string local_path = TWFunc::GetRootPath(path);

  // Iterate through all partitions
  for (const TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) {
      ret = partition->Unmount(display_error, flags);
      found = true;
    } else if (partition->is_sub_partition_ && partition->sub_partition_of_ == local_path) {
      partition->Unmount(display_error, flags);
    }
  }
  if (found) {
    return ret;
  }
  if (display_error) {
    gui_msg(
        Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(
            local_path));
  } else {
    LOGINFO("Unmount: Unable to find partition for path '%s'\n", local_path.c_str());
  }
  return false;
}

int TWPartitionManager::IsMountedByPath(const std::string& Path) const {
  if (const TWPartition* part = FindPartitionByPath(Path)) return part->IsMounted();
  LOGINFO("IsMounted: Unable to find partition for path '%s'\n", Path.c_str());
  return false;
}

int TWPartitionManager::MountCurrentStorage(const bool display_error) const {
  if (const std::string current_storage_path = DataManager::GetCurrentStoragePath();
    MountByPath(current_storage_path, display_error)) {
    if (const TWPartition* free_storage =
        FindPartitionByPath(current_storage_path))
      DataManager::SetValue(
          TW_STORAGE_FREE_SIZE, UnitConversion::FormatBytes(free_storage->free_));
    return true;
  }
  return false;
}

int TWPartitionManager::MountSettingsStorage(const bool display_error) const {
  return MountByPath(DataManager::GetSettingsStoragePath(), display_error);
}

TWPartition* TWPartitionManager::FindPartitionByPath(const std::string& Path) const {
  std::string local_path = TWFunc::GetRootPath(Path);

  if (local_path == "/system") local_path = GetAndroidRootPath();
  for (TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) return partition;
  }
  return nullptr;
}

TWPartition* TWPartitionManager::FindPartitionByBlockDevice(const std::string& block_device) const {
  for (TWPartition* partition : partitions_) {
    if (partition->primary_block_device_ == block_device ||
        (!partition->actual_block_device_.empty() && partition->actual_block_device_ ==
         block_device))
      return partition;
  }
  return nullptr;
}

bool TWPartitionManager::IsBackupNameValid(const std::string& backup_name, const bool display_error,
                                           const bool must_be_unique) {
  if (backup_name.size() > MAX_BACKUP_NAME_LEN) {
    if (display_error) gui_err("backup_name_len=Backup name is too long.");
    return false;
  }

  // "0" means use the current timestamp for the backup name.
  if (backup_name == "0") return true;

  // Valid set: space, 0-9, A-Z, a-z, -_.{}[]. Explicit checks (not the original ASCII
  // ranges 65-91/97-123) avoid admitting '['/'{' as the byte just past Z/z.
  auto is_valid = [](const char ch) {
    const auto c = static_cast<unsigned char>(ch);
    return c == ' '
           || (c >= '0' && c <= '9')
           || (c >= 'A' && c <= 'Z')
           || c == '[' || c == ']' || c == '_'
           || (c >= 'a' && c <= 'z')
           || c == '{' || c == '}' || c == '-' || c == '.';
  };
  for (const char ch : backup_name) {
    if (!is_valid(ch)) {
      if (display_error)
        gui_msg(
            Msg(msg::kError,
                "backup_name_invalid=Backup name '{1}' contains invalid character: '{2}'")(
                backup_name)(ch));
      return false;
    }
  }

  if (must_be_unique) {
    std::string backup_loc;
    DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, backup_loc);
    if (TWFunc::IsPathExists(fs::path(backup_loc) / backup_name)) {
      if (display_error) gui_err("backup_name_exists=A backup with that name already exists!");
      return false;
    }
  }
  return true;
}

bool TWPartitionManager::BackupPartition(PartitionSettings* part_settings) {
  if (part_settings->part == nullptr) return true;

  const std::string backup_log = fs::path(part_settings->backup_folder) / "recovery.log";
  int use_compression;
  DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);

  TWFunc::SetPerformanceMode(true);
  const time_t start = time(nullptr);

  // Back up a single partition (the main partition or one of its sub-partitions):
  // run the backup, sync, and (when enabled) generate its digest. part_settings->part
  // is set to the partition being processed so the caller's view stays in sync. A
  // false return replaces the original 'goto backup_error' early-exit.
  auto backup_one = [&](TWPartition* part) -> bool {
    part_settings->part = part;
    if (!part->Backup(part_settings, &tar_fork_pid)) return false;
    sync();
    sync();
    const std::string full = fs::path(part_settings->backup_folder) / part->backup_file_name_;
    if (part_settings->generate_digest) {
      if (!TwrpDigestDriver::MakeDigest(full)) return false;
    }
    return true;
  };

  const bool ok = [&] {
    TWPartition* parent = part_settings->part;
    if (!backup_one(parent)) return false;
    if (parent->has_sub_partition_) {
      for (TWPartition* subpart : partitions_) {
        if (subpart->can_be_backed_up_ && subpart->is_sub_partition_ &&
            subpart->sub_partition_of_ == parent->mount_point_) {
          if (!backup_one(subpart)) return false;
        }
      }
    }
    const time_t stop = time(nullptr);
    const int backup_time = static_cast<int>(difftime(stop, start));
    LOGINFO("Partition Backup time: %d\n", backup_time);
    if (part_settings->part->backup_method_ ==
        BackupMethod::kFiles)
      part_settings->file_time += backup_time;
    else part_settings->img_time += backup_time;
    return true;
  }();

  if (!ok) {
    CleanBackupFolder(part_settings->backup_folder);
    TWFunc::CopyFile("/tmp/recovery.log", backup_log, 0644);
    tw_set_default_metadata(backup_log.c_str());
  }
  TWFunc::SetPerformanceMode(false);
  return ok;
}

void TWPartitionManager::CleanBackupFolder(const std::string& backup_folder) {
  // Extensions we should delete when cleaning a failed backup.
  static constexpr const char* exts[] = { ".win", ".md5", ".sha2", ".info" };

  gui_msg("backup_clean=Backup Failed. Cleaning Backup Folder.");

  std::error_code ec;
  fs::directory_iterator it(backup_folder, ec);
  if (ec) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(backup_folder)(
            ec.message().c_str()));
    return;
  }

  for (const fs::directory_entry& entry : it) {
    for (const char* e : exts) {
      if (const auto& path = entry.path(); path.extension() == e) {
        if (unlink(path.c_str()) != 0)
          LOGINFO("Unable to unlink '%s: %s'\n", path.c_str(), strerror(errno));
        break;
      }
    }
  }
}

int TWPartitionManager::CheckBackupCancel() const {
  return stop_backup;
}

int TWPartitionManager::CancelBackup() {
  std::string full_backup_path;

  stop_backup = true;

  if (tar_fork_pid != 0) {
    std::string backup_name;
    std::string backup_folder;
    DataManager::GetValue(TW_BACKUP_NAME, backup_name);
    DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, backup_folder);
    full_backup_path = fs::path(backup_folder) / backup_name;
    LOGINFO("Killing pid: %d\n", tar_fork_pid);
    kill(tar_fork_pid, SIGUSR2);
    while (kill(tar_fork_pid, 0) == 0) {
      usleep(1000);
    }
    LOGINFO("Backup_Run stopped and returning false, backup cancelled.\n");
    LOGINFO("Removing directory %s\n", full_backup_path.c_str());
    TWFunc::RemoveDir(full_backup_path, false);
    tar_fork_pid = 0;
  }

  return 0;
}

bool TWPartitionManager::RunBackup() {
  PartitionSettings part_settings;
  std::string backup_name, backup_list;
  TWPartition* storage = nullptr;

  stop_backup = false;

  part_settings.img_bytes_remaining = 0;
  part_settings.file_bytes_remaining = 0;
  part_settings.img_time = 0;
  part_settings.file_time = 0;
  part_settings.img_bytes = 0;
  part_settings.file_bytes = 0;
  part_settings.pm_method = PartitionManagerOp::kBackup;

  const time_t total_start = time(nullptr);

  UpdateSystemDetails();

  if (!MountCurrentStorage(true)) return false;

  int skip_digest = 0;
  DataManager::GetValue(TW_SKIP_DIGEST_GENERATE_VAR, skip_digest);
  part_settings.generate_digest = skip_digest == 0;

  DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, part_settings.backup_folder);
  DataManager::GetValue(TW_BACKUP_NAME, backup_name);
  if (backup_name == gui_lookup("curr_date", "(Current Date)")) {
    backup_name = TWFunc::GetCurrentDate();
  } else if (backup_name == gui_lookup("auto_generate", "(Auto Generate)") || backup_name == "0" ||
             backup_name.empty()) {
    TWFunc::AutoGenerateBackupName();
    DataManager::GetValue(TW_BACKUP_NAME, backup_name);
  }

  LOGINFO("Backup Name is: '%s'\n", backup_name.c_str());

  part_settings.backup_folder = (fs::path(part_settings.backup_folder) / backup_name).string();

  LOGINFO("Backup_Folder is: '%s'\n", part_settings.backup_folder.c_str());

  LOGINFO("Calculating backup details...\n");
  DataManager::GetValue("tw_backup_list", backup_list);
  LOGINFO("Backup_List: %s\n", backup_list.c_str());

  // tw_backup_list is ';'-joined and ';'-terminated, e.g. "/system;/data;/".
  const std::vector<std::string> backup_paths = TWFunc::SplitString(backup_list, ';', true);

  int partition_count = 0;
  for (const std::string& path : backup_paths) {
    LOGINFO("backup_path: %s\n", path.c_str());
    TWPartition* part = FindPartitionByPath(path);
    if (!part) {
      gui_msg(Msg(msg::kError,
                  "unable_to_locate_partition=Unable to locate '{1}' partition for backup "
                  "calculations.")(path));
      continue;
    }

    partition_count++;
    if (part->backup_method_ ==
        BackupMethod::kFiles)
      part_settings.file_bytes += part->backup_size_;
    else part_settings.img_bytes += part->backup_size_;

    if (part->has_sub_partition_) {
      auto is_backup_sub_partition = [&](const TWPartition* subpart) {
        return subpart->can_be_backed_up_ && subpart->is_present_ && subpart->is_sub_partition_ &&
               subpart->sub_partition_of_ == part->mount_point_;
      };
      for (TWPartition* subpart : partitions_ | std::views::filter(is_backup_sub_partition)) {
        partition_count++;
        if (subpart->backup_method_ ==
            BackupMethod::kFiles)
          part_settings.file_bytes += subpart->backup_size_;
        else part_settings.img_bytes += subpart->backup_size_;
      }
    }
  }

  if (partition_count == 0) {
    gui_msg("no_partition_selected=No partitions selected for backup.");
    return false;
  }
  const uint64_t total_bytes = part_settings.file_bytes + part_settings.img_bytes;
  ProgressTracking progress(total_bytes);
  part_settings.progress = &progress;

  gui_msg(Msg("total_partitions_backup= * Total number of partitions to back up: {1}")(
      partition_count));
  gui_msg(Msg("total_backup_size= * Total size of all data: {1}")(
      UnitConversion::FormatBytes(total_bytes)));

  storage = FindPartitionByPath(DataManager::GetCurrentStoragePath());
  if (!storage) {
    gui_err("unable_locate_storage=Unable to locate storage device.");
    return false;
  }
  const uint64_t free_space = storage->free_;
  gui_msg(Msg("available_space= * Available space: {1}")(UnitConversion::FormatBytes(free_space)));

  int disable_free_space_check = 0;
  DataManager::GetValue(TW_DISABLE_FREE_SPACE_VAR, disable_free_space_check);

  // We require an extra 32MB just in case
  if (!disable_free_space_check && free_space - 32 * kMiB < total_bytes) {
    gui_err("no_space=Not enough free space on storage.");
    return false;
  }
  part_settings.img_bytes_remaining = part_settings.img_bytes;
  part_settings.file_bytes_remaining = part_settings.file_bytes;

  gui_msg("backup_started=[BACKUP STARTED]");

  int is_decrypted = 0;
  int is_encrypted = 0;

  DataManager::GetValue(TW_IS_DECRYPTED, is_decrypted);
  DataManager::GetValue(TW_IS_ENCRYPTED, is_encrypted);
  bool backup_folder_made = false;
  if (!is_encrypted || is_decrypted) {
    gui_msg(Msg("backup_folder= * Backup Folder: {1}")(part_settings.backup_folder));
    if (!TWFunc::RecursiveMkdir(part_settings.backup_folder)) {
      gui_err("fail_backup_folder=Failed to make backup folder.");
      return false;
    }
    backup_folder_made = true;
  }

  // Half of a backup restores to half of a system, so do not leave one in
  // the list looking like something that could be restored.
  auto discard_unfinished_backup = [&] {
    if (backup_folder_made && TWFunc::RemoveDir(part_settings.backup_folder, false) != 0)
      LOGERR("Unable to remove '%s'\n", part_settings.backup_folder.c_str());
  };

  DataManager::SetProgress(0.0);

  for (const std::string& path : backup_paths) {
    if (stop_backup) {
      discard_unfinished_backup();
      return false;
    }
    TWPartition* part = FindPartitionByPath(path);
    if (!part) {
      gui_msg(Msg(msg::kError,
                  "unable_to_locate_partition=Unable to locate '{1}' partition for backup "
                  "calculations.")(path));
      continue;
    }
    part_settings.part = part;
    if (!BackupPartition(&part_settings)) {
      discard_unfinished_backup();
      return false;
    }
  }

  // Average BPS
  if (part_settings.img_time == 0) part_settings.img_time = 1;
  if (part_settings.file_time == 0) part_settings.file_time = 1;
  uint64_t img_bps = part_settings.img_bytes / part_settings.img_time;
  uint64_t file_bps = part_settings.file_bytes / part_settings.file_time;

  if (part_settings.file_bytes != 0)
    gui_msg(Msg("avg_backup_fs=Average backup rate for file systems: {1}")(
        UnitConversion::FormatBytesPerSecond(file_bps)));
  if (part_settings.img_bytes != 0)
    gui_msg(Msg("avg_backup_img=Average backup rate for imaged drives: {1}")(
        UnitConversion::FormatBytesPerSecond(img_bps)));

  const time_t total_stop = time(nullptr);
  const int total_time = static_cast<int>(difftime(total_stop, total_start));

  TWExclude twe;
  const uint64_t actual_backup_size = twe.Get_Folder_Size(part_settings.backup_folder);

  int prev_img_bps = 0;
  DataManager::GetValue(TW_BACKUP_AVG_IMG_RATE, prev_img_bps);
  // Blend this run's rate into the stored history: 20% new, 80% previous.
  img_bps = (img_bps + prev_img_bps * 4) / 5;

  int use_compression = 0;
  DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
  uint64_t prev_file_bps = 0;
  if (use_compression) DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, prev_file_bps);
  else DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, prev_file_bps);
  file_bps = (file_bps + prev_file_bps * 4) / 5;

  DataManager::SetValue(TW_BACKUP_AVG_IMG_RATE, img_bps);
  if (use_compression) DataManager::SetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
  else DataManager::SetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

  gui_msg(Msg("total_backed_size=[{1} TOTAL BACKED UP]")(
      UnitConversion::FormatBytes(actual_backup_size)));
  UpdateSystemDetails();
  UnmountMainPartitions();
  gui_msg(Msg(msg::kHighlight, "backup_completed=[BACKUP COMPLETED IN {1} SECONDS]")(total_time));

  const std::string backup_log = (fs::path(part_settings.backup_folder) / "recovery.log").string();
  TWFunc::CopyFile("/tmp/recovery.log", backup_log, 0644);
  tw_set_default_metadata(backup_log.c_str());

  return true;
}

bool TWPartitionManager::RestorePartition(PartitionSettings* part_settings) const {
  time_t Start, Stop;

  TWFunc::SetPerformanceMode(true);

  time(&Start);

  if (!part_settings->part->Restore(part_settings)) {
    TWFunc::SetPerformanceMode(false);
    return false;
  }
  if (part_settings->part->has_sub_partition_) {
    const TWPartition* parentPart = part_settings->part;

    for (TWPartition* subpart : partitions_) {
      part_settings->part = subpart;
      if (subpart->is_sub_partition_ && subpart->sub_partition_of_ == parentPart->mount_point_) {
        part_settings->part = subpart;
        part_settings->part->SetBackupFileName(
            std::format("{}.{}.win", part_settings->part->backup_name_,
                        part_settings->part->current_file_system_));
        if (!subpart->Restore(part_settings)) {
          TWFunc::SetPerformanceMode(false);
          return false;
        }
      }
    }
  }
  time(&Stop);
  TWFunc::SetPerformanceMode(false);
  gui_msg(Msg("restore_part_done=[{1} done ({2} seconds)]")(
      part_settings->part->backup_display_name_)(
      static_cast<int>(difftime(Stop, Start))));

  return true;
}

int TWPartitionManager::RunRestore(const std::string& restore_name) const {
  time_t rStart, rStop;
  time(&rStart);
  std::string restore_list, restore_path;
  size_t start_pos = 0, end_pos;

  PartitionSettings part_settings = {
    .part = nullptr,
    .backup_folder = restore_name,
    .total_restore_size = 0,
    .partition_count = 0,
    .pm_method = PartitionManagerOp::kRestore,
  };

  gui_msg("restore_started=[RESTORE STARTED]");
  gui_msg(Msg("restore_folder=Restore folder: '{1}'")(restore_name));

  if (!MountCurrentStorage(true)) return false;

  int check_digest;
  DataManager::GetValue(TW_SKIP_DIGEST_CHECK_VAR, check_digest);
  if (check_digest > 0) {
    // Check Digest files first before restoring to ensure that all of them match before starting a restore
    TWFunc::GuiOperationText(TW_VERIFY_DIGEST_TEXT, gui_parse_text("{@verifying_digest}"));
    gui_msg("verifying_digest=Verifying Digest");
  } else {
    gui_msg("skip_digest=Skipping Digest check based on user setting.");
  }
  gui_msg("calc_restore=Calculating restore details...");
  DataManager::GetValue("tw_restore_selected", restore_list);

  if (!restore_list.empty()) {
    end_pos = restore_list.find(';', start_pos);
    while (end_pos != std::string::npos && start_pos < restore_list.size()) {
      restore_path = restore_list.substr(start_pos, end_pos - start_pos);
      part_settings.part = FindPartitionByPath(restore_path);
      if (part_settings.part) {
        if (part_settings.part->mount_read_only_) {
          gui_msg(Msg(msg::kError, "restore_read_only=Cannot restore {1} -- mounted read only.")(
              part_settings.part->backup_display_name_));
          return false;
        }

        auto full_filename = fs::path(part_settings.backup_folder) / part_settings.part->
                             backup_file_name_;

        if (tw_get_default_metadata(GetAndroidRootPath().c_str()) != 0) {
          gui_msg(Msg(msg::kWarning,
                      "restore_system_context=Unable to get default context for {1} -- Android may not boot.")(
              GetAndroidRootPath()));
        }

        if (check_digest > 0 && !TwrpDigestDriver::CheckDigest(full_filename)) return false;
        part_settings.partition_count++;
        part_settings.total_restore_size += part_settings.part->GetRestoreSize(&part_settings);
        if (part_settings.part->has_sub_partition_) {
          const TWPartition* parentPart = part_settings.part;

          for (TWPartition* subpart : partitions_) {
            part_settings.part = subpart;
            if (subpart->is_sub_partition_ && subpart->sub_partition_of_ == parentPart->
                mount_point_) {
              if (check_digest > 0 && !TwrpDigestDriver::CheckDigest(full_filename)) return false;
              part_settings.total_restore_size += subpart->GetRestoreSize(&part_settings);
            }
          }
        }
      } else {
        gui_msg(
            Msg(msg::kError,
                "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(
                restore_path));
      }
      start_pos = end_pos + 1;
      end_pos = restore_list.find(';', start_pos);
    }
  }

  if (part_settings.partition_count == 0) {
    gui_err("no_part_restore=No partitions selected for restore.");
    return false;
  }

  gui_msg(Msg("restore_part_count=Restoring {1} partitions...")(part_settings.partition_count));
  gui_msg(Msg("total_restore_size=Total restore size is {1}")(
      UnitConversion::FormatBytes(part_settings.total_restore_size)));
  DataManager::SetProgress(0.0);
  ProgressTracking progress(part_settings.total_restore_size);
  part_settings.progress = &progress;

  start_pos = 0;
  if (!restore_list.empty()) {
    end_pos = restore_list.find(';', start_pos);
    while (end_pos != std::string::npos && start_pos < restore_list.size()) {
      restore_path = restore_list.substr(start_pos, end_pos - start_pos);

      part_settings.part = FindPartitionByPath(restore_path);
      if (part_settings.part) {
        part_settings.partition_count++;
        if (!RestorePartition(&part_settings)) return false;
      } else {
        gui_msg(
            Msg(msg::kError,
                "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(
                restore_path));
      }
      start_pos = end_pos + 1;
      end_pos = restore_list.find(';', start_pos);
    }
  }
  TWFunc::GuiOperationText(
      TW_UPDATE_SYSTEM_DETAILS_TEXT, gui_parse_text("{@updating_system_details}"));
  tw_set_default_metadata(GetAndroidRootPath().c_str());
  UnmountByPath(GetAndroidRootPath(), false);
  UpdateSystemDetails();
  UnmountMainPartitions();
  time(&rStop);
  gui_msg(Msg(msg::kHighlight, "restore_completed=[RESTORE COMPLETED IN {1} SECONDS]")(
      static_cast<int>(difftime(rStop, rStart))));
  if (const TWPartition* DecryptData = FindPartitionByPath("/data");
    DecryptData && DecryptData->is_encrypted_)
    gui_msg(Msg(
        msg::kWarning,
        "reboot_after_restore=It is recommended to reboot Android once after first boot."));
  DataManager::SetValue("tw_file_progress", "");

  return true;
}

void TWPartitionManager::SetRestoreFiles(const std::string& restore_name) const {
  // Start with the default values
  std::string restore_list;
  bool get_date = true, check_encryption = true;

  DataManager::SetValue("tw_restore_encrypted", 0);
  const std::string android_root = GetAndroidRootPath();

  std::error_code ec;
  fs::directory_iterator it(restore_name, ec);
  if (ec) {
    gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(restore_name)(
        ec.message().c_str()));
    return;
  }

  for (const fs::directory_entry& entry : it) {
    const std::string name = entry.path().filename().string();
    if (name.size() <= 2) continue;
    const std::string file_path = fs::path(restore_name) / name;

    if (get_date) {
      struct stat st{};
      stat(file_path.c_str(), &st);
      DataManager::SetValue(TW_RESTORE_FILE_DATE, ctime(&st.st_mtime));
      get_date = false;
    }

    // Backup file names are <label>.<fstype>.<extn>; split on the first two dots.
    const std::string_view sv = name;
    const auto first_dot = sv.find('.');
    if (first_dot == std::string_view::npos) continue;
    const auto second_dot = sv.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) continue;
    const std::string_view fstype = sv.substr(first_dot + 1, second_dot - first_dot - 1);
    const std::string_view extn = sv.substr(second_dot + 1);

    if (fstype == "log") continue;
    const size_t extn_len = extn.size();
    if (extn_len != 3 && extn_len != 6) continue;
    if (!extn.starts_with("win")) continue;

    if (check_encryption) {
      if (TWFunc::GetFileType(file_path) == ENCRYPTED) {
        LOGINFO("'%s' is encrypted\n", file_path.c_str());
        DataManager::SetValue("tw_restore_encrypted", 1);
      }
    }
    // Keep single-file backups and the first part of a split archive.
    if (extn_len == 6 && extn != "win000") continue;

    const std::string label(sv.substr(0, first_dot));
    TWPartition* part = FindPartitionByPath(label);
    if (part == nullptr) {
      gui_msg(
          Msg(msg::kError,
              "unable_locate_part_backup_name=Unable to locate partition by backup name: '{1}'")(
              label));
      continue;
    }

    part->backup_file_name_ = name;
    if (extn_len > 3) {
      // Trim the split-archive index so backup_file_name_ ends in ".win".
      part->backup_file_name_.resize(name.size() - (extn_len - 3));
    }

    if (!part->is_sub_partition_) {
      if (part->backup_path_ == android_root) restore_list += "/system;";
      else restore_list += part->backup_path_ + ";";
    }
  }

  // Set the final value
  DataManager::SetValue("tw_restore_list", restore_list);
  DataManager::SetValue("tw_restore_selected", restore_list);
}

int TWPartitionManager::WipeByPath(const std::string& Path) const {
  int ret = false;
  bool found = false;
  std::string local_path = TWFunc::GetRootPath(Path);

  if (local_path == "/system") local_path = GetAndroidRootPath();
  if (Path == "/cache") {
    if (const TWPartition* cache = FindPartitionByPath("/cache"); !cache) {
      if (TWPartition* dat = FindPartitionByPath("/data")) {
        dat->WipeDataCache();
        found = true;
      }
    }
  }
  // Iterate through all partitions
  for (TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) {
      // iterate through all partitions since some legacy devices uses other partitions as vendor causes issues while wiping
      partition->FindActualBlockDevice();
      for (TWPartition* part1 : partitions_) {
        part1->FindActualBlockDevice();
        if (partition->actual_block_device_ == part1->actual_block_device_ &&
            partition->mount_point_ != part1->mount_point_)
          part1->Unmount(false);
      }
      if (Path == "/and-sec") ret = partition->WipeAndSec();
      else ret = partition->Wipe();
      found = true;
    } else if (partition->is_sub_partition_ && partition->sub_partition_of_ == local_path) {
      partition->Wipe();
    }
  }
  if (found) {
    return ret;
  }
  gui_msg(
      Msg(msg::kError,
          "unable_find_part_path=Unable to find partition for path '{1}'")(local_path));
  return false;
}

int TWPartitionManager::WipeByPath(const std::string& Path,
                                   const std::string& new_file_system) const {
  int ret = false;
  bool found = false;
  const std::string local_path = TWFunc::GetRootPath(Path);

  // Iterate through all partitions
  for (TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) {
      if (Path == "/and-sec") ret = partition->WipeAndSec();
      else ret = partition->Wipe(new_file_system);
      found = true;
    } else if (partition->is_sub_partition_ && partition->sub_partition_of_ == local_path) {
      partition->Wipe(new_file_system);
    }
  }
  if (found) {
    return ret;
  }
  gui_msg(
      Msg(msg::kError,
          "unable_find_part_path=Unable to find partition for path '{1}'")(local_path));
  return false;
}

int TWPartitionManager::FactoryReset() const {
  int ret = true;

  for (TWPartition* partition : partitions_) {
    if (partition->wipe_during_factory_reset_ && partition->is_present_) {
      if (!partition->Wipe()) ret = false;
    } else if (partition->has_android_secure_) {
      if (!partition->WipeAndSec()) ret = false;
    }
  }
  TWFunc::CheckAndRunScript("/system/bin/factoryreset.sh", "Factory Reset Script");
  return ret;
}

int TWPartitionManager::WipeDalvikCache() const {
  std::vector<std::string> dir;

  if (!MountByPath("/data", true)) return false;

  dir.emplace_back("/data/dalvik-cache");

  const std::string cacheDir = TWFunc::GetLogDir();
  if (cacheDir == CACHE_LOGS_DIR) {
    if (!PartitionManager.MountByPath(CACHE_LOGS_DIR, false)) {
      LOGINFO("Unable to mount %s for wiping cache.\n", CACHE_LOGS_DIR);
    }
    dir.push_back(cacheDir + "dalvik-cache");
    dir.push_back(cacheDir + "/dc");
  }

  TWPartition* sdext = FindPartitionByPath("/sd-ext");
  if (sdext && sdext->is_present_ && sdext->Mount(false)) {
    if (TWFunc::IsPathExists("/sd-ext/dalvik-cache")) {
      dir.emplace_back("/sd-ext/dalvik-cache");
    }
  }

  if (cacheDir == CACHE_LOGS_DIR) {
    gui_msg("wiping_cache_dalvik=Wiping Dalvik Cache Directories...");
  } else {
    gui_msg("wiping_dalvik=Wiping Dalvik Directory...");
  }
  for (const auto& d : dir) {
    if (TWFunc::IsPathExists(d)) {
      TWFunc::RemoveDir(d, false);
      gui_msg(Msg("cleaned=Cleaned: {1}...")(d));
    }
  }

  if (cacheDir == CACHE_LOGS_DIR) {
    gui_msg("cache_dalvik_done=-- Dalvik Cache Directories Wipe Complete!");
  } else {
    gui_msg("dalvik_done=-- Dalvik Directory Wipe Complete!");
  }

  return true;
}

int TWPartitionManager::WipeRotateData() const {
  if (!MountByPath("/data", true)) return false;

  unlink("/data/misc/akmd*");
  unlink("/data/misc/rild*");
  gui_print("Rotation data wiped.\n");
  return true;
}

int TWPartitionManager::WipeBatteryStats() const {
  if (!MountByPath("/data", true)) return false;

  if (!TWFunc::IsPathExists("/data/system/batterystats.bin")) {
    gui_print("No Battery Stats Found. No Need To Wipe.\n");
  } else {
    remove("/data/system/batterystats.bin");
    gui_print("Cleared battery stats.\n");
  }
  return true;
}

int TWPartitionManager::WipeAndroidSecure() const {
  int ret = false;
  bool found = false;

  // Iterate through all partitions
  for (TWPartition* partition : partitions_) {
    if (partition->has_android_secure_) {
      ret = partition->WipeAndSec();
      found = true;
    }
  }
  if (found) {
    return ret;
  }
  gui_err("no_andsec=No android secure partitions found.");
  return false;
}

int TWPartitionManager::FormatData() {
  TWPartition* dat = FindPartitionByPath("/data");
  TWPartition* metadata = FindPartitionByPath("/metadata");
  if (metadata) metadata->Unmount(false);

  if (dat) {
    int ret = false;
    if (android::base::GetBoolProperty("ro.virtual_ab.enabled", false)) {
#ifndef TW_EXCLUDE_APEX
      TwrpApex::Unmount();
#endif
      if (metadata) metadata->Mount(true);
      if (!CheckPendingMerges()) return false;
    }
    ret = dat->WipeEncryption();
    if (ret) TWFunc::CheckAndRunScript("/system/bin/formatdata.sh", "Format Data Script");
    return ret;
  }
  gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/data"));
  return false;
}

int TWPartitionManager::WipeMediaFromData() const {
  if (TWPartition* dat = FindPartitionByPath("/data")) {
    if (!dat->has_data_media_) {
      LOGERR("This device does not have /data/media\n");
      return false;
    }
    if (!dat->Mount(true)) return false;

    gui_msg("wiping_datamedia=Wiping internal storage -- /data/media...");
    RemoveMtpStorage(dat->mtp_storage_id_);
    TWFunc::RemoveDir("/data/media", false);
    dat->RecreateMediaFolder();
    AddMtpStorage(dat->mtp_storage_id_);
    return true;
  }
  gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/data"));
  return false;
}

int TWPartitionManager::RepairByPath(const std::string& Path, const bool display_error) const {
  int ret = false;
  bool found = false;
  const std::string local_path = TWFunc::GetRootPath(Path);

  if (local_path == "/tmp" || local_path == "/") return true;

  // Iterate through all partitions
  for (TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) {
      ret = partition->Repair();
      found = true;
    } else if (partition->is_sub_partition_ && partition->sub_partition_of_ == local_path) {
      partition->Repair();
    }
  }
  if (found) {
    return ret;
  }
  if (display_error) {
    gui_msg(
        Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(
            local_path));
  } else {
    LOGINFO("Repair: Unable to find partition for path '%s'\n", local_path.c_str());
  }
  return false;
}

int TWPartitionManager::ResizeByPath(const std::string& Path, const bool display_error) const {
  int ret = false;
  bool found = false;
  const std::string local_path = TWFunc::GetRootPath(Path);

  if (local_path == "/tmp" || local_path == "/") return true;

  // Iterate through all partitions
  for (TWPartition* partition : partitions_) {
    if (MatchesPath(partition, local_path)) {
      ret = partition->Resize();
      found = true;
    } else if (partition->is_sub_partition_ && partition->sub_partition_of_ == local_path) {
      partition->Resize();
    }
  }
  if (found) {
    return ret;
  }
  if (display_error) {
    gui_msg(
        Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(
            local_path));
  } else {
    LOGINFO("Resize: Unable to find partition for path '%s'\n", local_path.c_str());
  }
  return false;
}

void TWPartitionManager::UpdateSystemDetails(const bool defer_data_size,
                                             const bool display_error) const {
  uint64_t data_size = 0;
  TWPartition* deferred = nullptr;

  gui_msg("update_part_details=Updating partition details...");
  for (TWPartition* partition : partitions_) {
    // Only /data feeds TW_BACKUP_DATA_SIZE, so only /data is worth deferring.
    const bool defer = defer_data_size && partition->has_data_media_
                       && partition->mount_point_ == "/data";
    partition->UpdateSize(display_error, defer);
    if (defer) deferred = partition;
    if (partition->can_be_mounted_) {
      if (partition->mount_point_ == GetAndroidRootPath()) {
        DataManager::SetValue(TW_BACKUP_SYSTEM_SIZE, partition->backup_size_);
      } else if (partition->mount_point_ == "/data" || partition->mount_point_ == "/datadata") {
        data_size += partition->backup_size_;
      } else if (partition->mount_point_ == "/cache") {
        DataManager::SetValue(TW_BACKUP_CACHE_SIZE, partition->backup_size_);
      } else if (partition->mount_point_ == "/sd-ext") {
        DataManager::SetValue(TW_BACKUP_SDEXT_SIZE, partition->backup_size_);
        if (partition->backup_size_ == 0) {
          DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
          DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
        } else DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
      } else if (partition->has_android_secure_) {
        DataManager::SetValue(TW_BACKUP_ANDSEC_SIZE, partition->backup_size_);
        if (partition->backup_size_ == 0) {
          DataManager::SetValue(TW_HAS_ANDROID_SECURE, 0);
          DataManager::SetValue(TW_BACKUP_ANDSEC_VAR, 0);
        } else DataManager::SetValue(TW_HAS_ANDROID_SECURE, 1);
      } else if (partition->mount_point_ == "/boot") {
        DataManager::SetValue(TW_BACKUP_BOOT_SIZE, partition->backup_size_);
        if (partition->backup_size_ == 0) {
          DataManager::SetValue("tw_has_boot_partition", 0);
          DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
        } else DataManager::SetValue("tw_has_boot_partition", 1);
      }
    } else {
      // Handle unmountable partitions in case we reset defaults
      if (partition->mount_point_ == "/boot") {
        DataManager::SetValue(TW_BACKUP_BOOT_SIZE, partition->backup_size_);
        if (partition->backup_size_ == 0) {
          DataManager::SetValue(TW_HAS_BOOT_PARTITION, 0);
          DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
        } else DataManager::SetValue(TW_HAS_BOOT_PARTITION, 1);
      } else if (partition->mount_point_ == "/recovery") {
        DataManager::SetValue(TW_BACKUP_RECOVERY_SIZE, partition->backup_size_);
        if (partition->backup_size_ == 0) {
          DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 0);
          DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
        } else DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 1);
      } else if (partition->mount_point_ == "/data") {
        data_size += partition->backup_size_;
      }
    }
  }
  gui_msg("update_part_details_done=...done");
  DataManager::SetValue(TW_BACKUP_DATA_SIZE, data_size);
  const std::string current_storage_path = DataManager::GetCurrentStoragePath();
  if (TWPartition* free_storage = FindPartitionByPath(current_storage_path)) {
    // Attempt to mount storage
    if (!free_storage->Mount(false)) {
      gui_msg(Msg(msg::kWarning, "unable_to_mount_storage=Unable to mount storage"));
      DataManager::SetValue(TW_STORAGE_FREE_SIZE, UnitConversion::FormatBytes(0));
    } else {
      DataManager::SetValue(TW_STORAGE_FREE_SIZE, UnitConversion::FormatBytes(free_storage->free_));
    }
  } else {
    LOGINFO("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
  }
  if (!WriteFstab())
    LOGERR("Error creating fstab\n");
  // A locked /data holds nothing a backup could restore, so leave the walk
  // until something actually asks for the number.
  if (deferred && !TWPartition::DataIsLocked()) deferred->UpdateDataSizeAsync();
}

void TWPartitionManager::ProcessAsyncDataSize() const {
  if (TWPartition* dat = FindPartitionByPath("/data")) dat->ApplyAsyncDataSize();
}

void TWPartitionManager::PostDecrypt(const std::string& block_device) const {
  if (TWPartition* dat = FindPartitionByPath("/data"); dat) {
    // reparse for /cache/recovery/command
    static constexpr auto COMMAND_FILE = "/data/cache/command";
    if (TWFunc::IsPathExists(COMMAND_FILE)) {
      StartupArgs startup;
      std::string content;
      TWFunc::ReadFile(COMMAND_FILE, content);
      const std::vector args = { content };
      startup.ProcessRecoveryArgs(args, 0);
    }

    DataManager::SetValue(TW_IS_DECRYPTED, 1);
    dat->is_decrypted_ = true;
    if (!block_device.empty()) {
      dat->decrypted_block_device_ = block_device;
      gui_msg(
          Msg("decrypt_success_dev=Data successfully decrypted, new block device: '{1}'")(
              block_device));
    } else {
      gui_msg("decrypt_success_nodev=Data successfully decrypted");
    }
    android::base::SetProperty("twrp.decrypt.done", "true");
    dat->SetupFileSystem(false);
    dat->current_file_system_ = dat->fstab_file_system_;
    // Needed if we're ignoring blkid because encrypted devices start out as emmc

    // Mount only /data
    dat->symlink_path_ = ""; // Not to let it to bind mount /data/media again
    // The mount is what the wait here was ever for, so wait on that.
    int retry_count = 20;
    while (!dat->Mount(false) && --retry_count) usleep(50000);
    if (!dat->Mount(false)) {
      LOGERR("Unable to mount /data after decryption");
    }

    if (dat->has_data_media_ && TWFunc::IsPathExists("/data/media/0")) {
      dat->storage_path_ = "/data/media/0";
    } else {
      dat->storage_path_ = "/data/media";
    }
    dat->symlink_path_ = dat->storage_path_;
    DataManager::SetValue("tw_storage_path", dat->symlink_path_);
    DataManager::SetValue("tw_settings_path", TW_STORAGE_PATH);
    LOGINFO("New storage path after decryption: %s\n", dat->storage_path_.c_str());

    DataManager::LoadTWRPFolderInfo();
    UpdateSystemDetails(true);
    OutputPartition(dat);
    if (!android::base::StartsWith(dat->actual_block_device_, "/dev/block/mmcblk")) {
      if (!dat->BindMount())
        LOGERR("Unable to bind mount /sdcard to %s\n", dat->storage_path_.c_str());
    }
  } else
    LOGERR("Unable to locate data partition.\n");
}

void TWPartitionManager::ParseUsers() {
#ifdef TW_INCLUDE_FBE
  // twrp.user.<id>.decrypt is set by vold's load_all_de_keys for each user with a DE key.
  // Enumerate /data/system/users for <id>.xml profiles instead of polling 0..9999, then
  // let the property gate which of those users are in scope for decryption.
  const fs::path users_dir = "/data/system/users";
  for (std::error_code dir_ec; const auto& entry : fs::directory_iterator(users_dir, dir_ec)) {
    if (!entry.is_regular_file(dir_ec)) continue;

    auto name = entry.path().filename();
    if (name.extension() != ".xml") continue; // also skips per-user <id>/ subdirs
    std::string stem = name.stem();
    int user_id = 0;
    if (auto [ptr, ec] = std::from_chars(stem.data(), stem.data() + stem.size(), user_id);
      ec != std::errc{} || ptr != stem.data() + stem.size())
      continue; // non-numeric id
    if (user_id < 0 || user_id > 9999) continue; // preserve TWRP's id-range contract

    const std::string val = android::base::GetProperty(
        std::format("twrp.user.{}.decrypt", user_id), "-1");
    if (val == "-1") continue; // property still gates inclusion

    // Resolve the display name with early returns instead of a 5-level pyramid.
    auto resolve_user_name = [users_dir](int id) -> std::string {
      const std::string& id_str = std::to_string(id);
      fs::path path = users_dir / std::format("{}.xml", id);

      // Android 12+ stores ABX (binary XML) profiles; convert to a readable temp file.
      if (!TWFunc::CheckXmlFormat(path)) {
        if (std::string converted; TWFunc::AbxToXml(path, converted)) {
          LOGINFO("Android 12+: '%s' converted to plain XML (user %d).\n",
                  path.c_str(), id);
          path = converted;
        }
      }

      const std::unique_ptr<char, decltype(&free)> buf(
          PageManager::LoadFileToBuffer(path.string(), nullptr), free);
      if (!buf) return id_str; // no/empty profile -> id fallback

      const auto doc = std::make_unique<rapidxml::xml_document<> >();
      doc->parse<0>(buf.get()); // non-throwing (parse_error_handler)
      const rapidxml::xml_node<>* user_node = doc->first_node("user");
      if (!user_node) return id_str;
      const rapidxml::xml_node<>* name_node = user_node->first_node("name");
      if (!name_node) return id_str;
      return std::format("{} ({})", name_node->value(), id);
    };

    UsersStruct user;
    user.user_id = user_id;
    user.user_name = resolve_user_name(user_id);
    std::string filename; // Get_Password_Type out-param (unused)
    user.type = android::keystore::Get_Password_Type(user_id, filename);
    user.is_decrypted = val == "1";
    users_list_.push_back(user);
  }
  CheckUsersDecryptionStatus();
#endif
}

std::vector<UsersStruct>* TWPartitionManager::GetUsersList() {
  return &users_list_;
}

void TWPartitionManager::MarkUserDecrypted(int user_id) {
#ifdef TW_INCLUDE_FBE
  for (UsersStruct& user : users_list_) {
    if (user.user_id == user_id) {
      user.is_decrypted = true;
      android::base::SetProperty(std::format("twrp.user.{}.decrypt", user_id), "1");
      break;
    }
  }
  CheckUsersDecryptionStatus();
#endif
}

void TWPartitionManager::MarkDataLocked() {
#ifdef TW_INCLUDE_FBE
  // No DE keys were loaded, so the unmount took none.
  if (!DataManager::GetIntValue(TW_IS_FBE)) return;
  for (UsersStruct& user : users_list_) {
    if (!user.is_decrypted) continue;
    // fscrypt_unlock_ce_storage() returns early for a user vold still holds
    // a policy for, so the next decrypt would install nothing.
    fscrypt_lock_ce_storage(user.user_id);
    user.is_decrypted = false;
    android::base::SetProperty(std::format("twrp.user.{}.decrypt", user.user_id), "0");
  }
  if (TWPartition* data = FindPartitionByPath("/data")) data->is_decrypted_ = false;
  CheckUsersDecryptionStatus();
  DataManager::SetValue(TW_IS_DECRYPTED, 0);
  DataManager::SetValue(TW_IS_ENCRYPTED, 1);
  android::base::SetProperty("twrp.decrypt.done", "");
  LOGINFO("Data is locked again, its keys did not survive the unmount.\n");
#endif
}

bool TWPartitionManager::StorageNameInUse(const std::string& Name) {
  return std::ranges::any_of(partitions_, [&](const TWPartition* partition) {
    return partition->storage_name_ == Name;
  });
}

void TWPartitionManager::CheckUsersDecryptionStatus() const {
#ifdef TW_INCLUDE_FBE
  int all_is_decrypted = !users_list_.empty();
  for (const UsersStruct& user : users_list_) {
    if (!user.is_decrypted) {
      LOGINFO("User %d is not decrypted.\n", user.user_id);
      all_is_decrypted = 0;
      break;
    }
  }
  if (all_is_decrypted == 1) {
    LOGINFO("All found users are decrypted.\n");
    DataManager::SetValue("tw_all_users_decrypted", "1");
    android::base::SetProperty("twrp.all.users.decrypted", "true");
  } else {
    DataManager::SetValue("tw_all_users_decrypted", "0");
    android::base::SetProperty("twrp.all.users.decrypted", "false");
  }
#endif
}

int TWPartitionManager::DecryptDevice(const std::string& Password, const int user_id) {
#ifdef TW_INCLUDE_CRYPTO
  // Mount any partitions that need to be mounted for decrypt
  for (TWPartition* partition : partitions_) {
    if (partition->mount_to_decrypt_) {
      partition->Mount(true);
    }
  }
  android::base::SetProperty("twrp.mount_to_decrypt", "1");

  SetCryptoState();

  if (DataManager::GetIntValue(TW_IS_FBE)) {
#ifdef TW_INCLUDE_FBE
    TWPartition* data = FindPartitionByPath("/data");
    if (data == nullptr) {
      LOGERR("Unable to locate /data for FBE decrypt\n");
      return -1;
    }
    const bool data_was_mounted = data->IsMounted();
    if (!MountByPath("/data", true)) return -1; // /data has to be mounted for FBE
    // Unmounting /data drops the DE keys. Every decrypt entry point must restore
    // them before gatekeeper files and CE policies can be read.
    if (!data->RestoreDeKeys()) {
      if (!data_was_mounted) UnmountByPath("/data", true);
      return -1;
    }

    bool user_found = false;
    bool user_need_decrypt = false;
    for (const UsersStruct& user : users_list_) {
      if (user.user_id == user_id) {
        user_found = true;
        user_need_decrypt = !user.is_decrypted;
        break;
      }
    }
    if (!user_found) {
      LOGERR("User %d was not discovered during FBE setup\n", user_id);
      return -1;
    }
    if (!user_need_decrypt) {
      LOGINFO("User %d does not require decryption\n", user_id);
      return 0;
    }

    int retry_count = 10;
    while (!TWFunc::IsPathExists("/data/system/users/gatekeeper.password.key")
           && --retry_count)
      usleep(2000);
    // A small sleep is needed after mounting /data to ensure reliable decrypt...maybe because of DE?
    gui_msg(Msg("decrypting_user_fbe=Attempting to decrypt FBE for user {1}...")(user_id));
    if (android::keystore::Decrypt_User(user_id, Password)) {
      gui_msg(Msg("decrypt_user_success_fbe=User {1} Decrypted Successfully")(user_id));
      MarkUserDecrypted(user_id);
      if (user_id == 0) PostDecrypt("");

      return 0;
    }
    gui_msg(Msg(msg::kError, "decrypt_user_fail_fbe=Failed to decrypt user {1}")(user_id));
#else
    LOGERR("FBE support is not present\n");
#endif
    return -1;
  }
  return -1;
#else
  gui_err("no_crypto_support=No crypto support was compiled into this build.");
  return -1;
#endif
}

int TWPartitionManager::FixContexts() const {
  for (TWPartition* partition : partitions_) {
    if (partition->has_data_media_) {
      if (partition->Mount(true)) {
        if (fixContexts::fixDataMediaContexts(partition->mount_point_) != 0) return -1;
      }
    }
  }
  UnmountMainPartitions();
  gui_msg("done=Done.");
  return 0;
}

TWPartition* TWPartitionManager::FindNextStorage(const std::string& Path,
                                                 const bool exclude_data_media) const {
  std::string search_path;
  bool after_match = Path.empty();
  if (!after_match) search_path = TWFunc::GetRootPath(Path);

  for (TWPartition* partition : partitions_) {
    if (!after_match) {
      // Still locating the Path marker. Skip this partition; when it
      // matches, flip the flag and still skip it (storage search starts
      // from the next partition, mirroring the original iter++/break).
      if (partition->mount_point_ == search_path) after_match = true;
      continue;
    }
    if (exclude_data_media && partition->has_data_media_) continue;
    if (partition->is_storage_ && partition->is_present_) return partition;
  }

  return nullptr;
}

int TWPartitionManager::OpenLunFile(const std::string& partition_path,
                                    const std::string& lun_file) const {
  const TWPartition* part = FindPartitionByPath(partition_path);

  if (!part) {
    LOGINFO("Unable to locate '%s' for USB storage mode.", partition_path.c_str());
    gui_msg(
        Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(
            partition_path));
    return false;
  }
  LOGINFO("USB mount '%s', '%s' > '%s'\n", partition_path.c_str(),
          part->actual_block_device_.c_str(),
          lun_file.c_str());
  if (!part->Unmount(true) || !part->is_present_) return false;

  if (!TWFunc::WriteToFile(lun_file, part->actual_block_device_)) {
    LOGERR("Unable to write to ums lunfile '%s': (%s)\n", lun_file.c_str(), strerror(errno));
    return false;
  }
  return true;
}

int TWPartitionManager::UsbStorageEnable() {
  char lun_file[255];
  bool has_multiple_lun = false;

  if (std::string(CUSTOM_LUN_FILE).find('%') != std::string::npos) {
    sprintf(lun_file, CUSTOM_LUN_FILE, 1);
    if (TWFunc::IsPathExists(lun_file)) has_multiple_lun = true;
  }
  mtp_was_enabled = TWFunc::ToggleMtp(false); // Must disable MTP for USB Storage
  // On error: restore MTP (if it was enabled before we disabled it) and report failure.
  auto mtp_restore_fail = [this] {
    if (mtp_was_enabled) if (!EnableMtp()) DisableMtp();
    return false;
  };
  if (!has_multiple_lun) {
    LOGINFO("Device doesn't have multiple lun files, mount current storage\n");
    sprintf(lun_file, CUSTOM_LUN_FILE, 0);
    if (TWFunc::GetRootPath(DataManager::GetCurrentStoragePath()) == "/data") {
      if (const TWPartition* Mount = FindNextStorage("", true)) {
        if (!OpenLunFile(Mount->mount_point_, lun_file)) {
          return mtp_restore_fail();
        }
      } else {
        gui_err("unable_locate_storage=Unable to locate storage device.");
        return mtp_restore_fail();
      }
    } else if (!OpenLunFile(DataManager::GetCurrentStoragePath(), lun_file)) {
      return mtp_restore_fail();
    }
  } else {
    LOGINFO("Device has multiple lun files\n");
    sprintf(lun_file, CUSTOM_LUN_FILE, 0);
    if (const TWPartition* Mount1 = FindNextStorage("", true)) {
      if (!OpenLunFile(Mount1->mount_point_, lun_file)) {
        return mtp_restore_fail();
      }
      sprintf(lun_file, CUSTOM_LUN_FILE, 1);
      if (const TWPartition* Mount2 = FindNextStorage(Mount1->mount_point_, true);
        Mount2 && Mount2->mount_point_ != Mount1->mount_point_) {
        OpenLunFile(Mount2->mount_point_, lun_file);
        // Mimic single lun code: Mount CurrentStoragePath if it's not /data
      } else if (TWFunc::GetRootPath(DataManager::GetCurrentStoragePath()) != "/data") {
        OpenLunFile(DataManager::GetCurrentStoragePath(), lun_file);
      }
      // Mimic single lun code: Mount CurrentStoragePath if it's not /data
    } else if (TWFunc::GetRootPath(DataManager::GetCurrentStoragePath()) != "/data" &&
               !OpenLunFile(DataManager::GetCurrentStoragePath(), lun_file)) {
      gui_err("unable_locate_storage=Unable to locate storage device.");
      return mtp_restore_fail();
    }
  }
  android::base::SetProperty("sys.storage.ums_enabled", "1");
  android::base::SetProperty("sys.usb.config", "mass_storage,adb");
  return true;
}

int TWPartitionManager::UsbStorageDisable() {
  int index, ret = 0;
  char lun_file[255];

  for (index = 0; index < 2; index++) {
    sprintf(lun_file, CUSTOM_LUN_FILE, index);
    if (const std::string str; !TWFunc::WriteToFile(lun_file, str)) {
      ret = -1;
      break;
    }
  }
  MountAllStorage();
  UpdateSystemDetails();
  UnmountMainPartitions();
  android::base::SetProperty("sys.storage.ums_enabled", "0");
  android::base::SetProperty("sys.usb.config", "adb");
  if (mtp_was_enabled && !EnableMtp()) DisableMtp();

  if (ret < 0 && index == 0) {
    LOGERR("Unable to write to ums lunfile '%s'.", lun_file);
    return false;
  }
  return true;
}

void TWPartitionManager::MountAllStorage() const {
  for (TWPartition* partition : partitions_) {
    if (partition->is_storage_) partition->Mount(false);
  }
}

void TWPartitionManager::UnmountMainPartitions() const {
  // Unmounts system and data if data is not data/media
  // Also unmounts boot if boot is mountable
  LOGINFO("Unmounting main partitions...\n");

  if (FindPartitionByPath("/vendor")) UnmountByPath("/vendor", false);
  if (FindPartitionByPath(GetAndroidRootPath())) UnmountByPath(GetAndroidRootPath(), true);
  if (FindPartitionByPath("/product")) UnmountByPath("/product", false);
  if (!datamedia) UnmountByPath("/data", true);

  if (const auto partition = FindPartitionByPath("/boot");
    partition && partition->can_be_mounted_)
    partition->Unmount(true);
}

int TWPartitionManager::PartitionSdCard() const {
  std::string Command, Device, fat_str, ext_str, ext_format;
  int ext = 0, swap = 0, total_size = 0, fat_size = 0;

  gui_msg("start_partition_sd=Partitioning SD Card...");

  // Locate and validate device to partition
  TWPartition* sdcard = FindPartitionByPath(DataManager::GetCurrentStoragePath());

  if (!sdcard) {
    gui_err("partition_sd_locate=Unable to locate device to partition.");
    return false;
  }

  if (!sdcard->removable_ || sdcard->has_data_media_) {
    gui_err("partition_sd_locate=Unable to locate device to partition.");
    return false;
  }

  // Unmount everything
  if (!sdcard->Unmount(true)) return false;
  TWPartition* SDext = FindPartitionByPath("/sd-ext");
  if (SDext) {
    if (!SDext->Unmount(true)) return false;
  }
  if (char* swap_path = getenv("SWAPPATH")) {
    LOGINFO("Unmounting swap at '%s'\n", swap_path);
    umount(swap_path);
  }

  // Determine block device
  if (sdcard->alternate_block_device_.empty()) {
    sdcard->FindActualBlockDevice();
    Device = sdcard->actual_block_device_;
    // Just use the root block device
    Device.resize(strlen("/dev/block/mmcblkX"));
  } else {
    Device = sdcard->alternate_block_device_;
  }

  // Find the size of the block device:
  total_size = static_cast<int>(TWFunc::GetBlockSizeByIoctl(Device.c_str()) / kMiB);

  DataManager::GetValue("tw_sdext_size", ext);
  DataManager::GetValue("tw_swap_size", swap);
  DataManager::GetValue("tw_sdpart_file_system", ext_format);
  fat_size = total_size - ext - swap;
  LOGINFO(
      "sd card mount point %s block device is '%s', sdcard size is: %s, fat size: %s, ext size: %s, ext system: '%s', swap size: %s\n",
      DataManager::GetCurrentStoragePath().c_str(), Device.c_str(),
      UnitConversion::FormatBytes(total_size).c_str(),
      UnitConversion::FormatBytes(fat_size).c_str(), UnitConversion::FormatBytes(ext).c_str(),
      ext_format.c_str(),
      UnitConversion::FormatBytes(swap).c_str());

  // Determine partition sizes
  if (swap == 0 && ext == 0) {
    fat_str = "-0";
  } else {
    fat_str = std::format("{}MB", fat_size);
  }
  if (swap == 0) {
    ext_str = "-0";
  } else {
    ext_str = std::format("+{}MB", ext);
  }

  if (ext + swap > total_size) {
    gui_err("ext_swap_size=EXT + Swap size is larger than sdcard size.");
    return false;
  }

  gui_msg("remove_part_table=Removing partition table...");
  Command = std::format("sgdisk --zap-all {}", Device);
  LOGINFO("Command is: '%s'\n", Command.c_str());
  if (TWFunc::ExecCmd(Command) != 0) {
    gui_err("unable_rm_part=Unable to remove partition table.");
    UpdateSystemDetails();
    return false;
  }
  gui_msg(Msg("create_part=Creating {1} partition...")("FAT32"));
  Command = std::format("sgdisk  --new=0:0:{} --change-name=0:\"Microsoft basic data\" "
                        "--typecode=0:EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 {}", fat_str, Device);
  LOGINFO("Command is: '%s'\n", Command.c_str());
  if (TWFunc::ExecCmd(Command) != 0) {
    gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("FAT32"));
    return false;
  }
  if (ext > 0) {
    gui_msg(Msg("create_part=Creating {1} partition...")("EXT"));
    Command = std::format("sgdisk --new=0:0:{} --change-name=0:\"Linux filesystem\" {}", ext_str,
                          Device);
    LOGINFO("Command is: '%s'\n", Command.c_str());
    if (TWFunc::ExecCmd(Command) != 0) {
      gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("EXT"));
      UpdateSystemDetails();
      return false;
    }
  }
  if (swap > 0) {
    gui_msg(Msg("create_part=Creating {1} partition...")("swap"));
    Command = std::format("sgdisk --new=0:0:-0 --change-name=0:\"Linux swap\" "
                          "--typecode=0:0657FD6D-A4AB-43C4-84E5-0933C84B4F4F {}", Device);
    LOGINFO("Command is: '%s'\n", Command.c_str());
    if (TWFunc::ExecCmd(Command) != 0) {
      gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("swap"));
      UpdateSystemDetails();
      return false;
    }
  }

  // Convert GPT to MBR
  Command = std::format("sgdisk --gpttombr {}", Device);
  if (TWFunc::ExecCmd(Command) != 0)
    LOGINFO("Failed to covert partition GPT to MBR\n");

  // Tell the kernel to rescan the partition table
  {
    const android::base::unique_fd fd(open(Device.c_str(), O_RDONLY));
    ioctl(fd.get(), BLKRRPART, 0);
  }

  std::string format_device = Device;
  if (Device.starts_with("/dev/block/mmcblk")) format_device += 'p';

  // Format new partitions to proper file system
  if (fat_size > 0) {
    Command = std::format("mkfs.fat {}1", format_device);
    TWFunc::ExecCmd(Command);
  }
  if (ext > 0) {
    if (!SDext) {
      Command = std::format("mke2fs -t {} -m 0 {}2", ext_format, format_device);
      gui_msg(Msg("format_sdext_as=Formatting sd-ext as {1}...")(ext_format));
      LOGINFO("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
      TWFunc::ExecCmd(Command);
    } else {
      SDext->Wipe(ext_format);
    }
  }
  if (swap > 0) {
    Command = std::format("mkswap {}{}", format_device, ext > 0 ? 3 : 2);
    TWFunc::ExecCmd(Command);
  }

  // UpdateSystemDetails() puts back whatever it had to mount itself, so
  // mount here to leave the card up the way partitioning always has.
  sdcard->Mount(true);

  UpdateSystemDetails();
  gui_msg("part_complete=Partitioning complete.");
  return true;
}

void TWPartitionManager::GetPartitionList(const std::string& list_type,
                                          std::vector<PartitionList>* partition_list) const {
  if (list_type == "mount") {
    for (const TWPartition* partition : partitions_) {
      // SetupFileSystem() calls everything mountable, so ask whether
      // there is a device to mount as well.
      if (partition->can_be_mounted_ && partition->is_present_) {
        partition_list->push_back({
          .display_name = partition->display_name_,
          .mount_point = partition->mount_point_,
          .selected = partition->IsMounted(),
        });
      }
    }
  } else if (list_type == "storage") {
    const std::string current_storage = DataManager::GetCurrentStoragePath();
    for (TWPartition* partition : partitions_) {
      // A uevent entry with nothing plugged into it is a place to put a
      // device, not a place to put files.
      if (partition->is_storage_ && partition->is_present_) {
        partition_list->push_back({
          .display_name = std::format("{} ({})", partition->storage_name_,
                                      UnitConversion::FormatBytes(partition->free_)),
          .mount_point = partition->storage_path_,
          .selected = partition->storage_path_ == current_storage,
        });
      }
    }
  } else if (list_type == "backup") {
    for (TWPartition* partition : partitions_) {
      if (partition->can_be_backed_up_ && !partition->is_sub_partition_ && partition->is_present_) {
        unsigned long long backup_size = partition->backup_size_;
        if (partition->has_sub_partition_) {
          for (const TWPartition* subpart : partitions_) {
            if (subpart->is_sub_partition_ && subpart->can_be_backed_up_ && subpart->is_present_
                && subpart->sub_partition_of_ == partition->mount_point_) {
              backup_size += subpart->backup_size_;
            }
          }
        }
        std::string size_place;
        if (partition->backup_size_provisional_ && !TWPartition::DataIsLocked()) {
          // The list is on screen, so someone wants the number now.
          partition->UpdateDataSizeAsync();
          size_place = gui_lookup("calculating", "calculating");
        } else {
          size_place = UnitConversion::FormatBytes(backup_size);
        }
        partition_list->push_back({
          .display_name = std::format("{} ({})", partition->backup_display_name_, size_place),
          .mount_point = partition->backup_path_,
          .selected = false,
        });
      }
    }
  } else if (list_type == "restore") {
    std::string restore_list;

    DataManager::GetValue("tw_restore_list", restore_list);
    if (!restore_list.empty()) {
      for (const std::string& restore_path : android::base::Tokenize(restore_list, ";")) {
        const TWPartition* restore_part = FindPartitionByPath(restore_path);
        if (!restore_part) {
          gui_msg(
              Msg(msg::kError,
                  "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(
                  restore_path));
          continue;
        }
        // Don't allow restore of recovery (causes problems on some devices)
        // Don't add subpartitions to the list of items
        if ((restore_part->backup_name_ == "recovery" && !restore_part->can_be_backed_up_) ||
            restore_part->
            is_sub_partition_) {
          continue;
        }
        partition_list->push_back({
          .display_name = restore_part->backup_display_name_,
          .mount_point = restore_part->backup_path_,
          .selected = true,
        });
      }
    }
  } else if (list_type == "wipe") {
    // dalvik
    partition_list->push_back({
      .display_name = gui_parse_text("{@dalvik}"),
      .mount_point = "DALVIK",
      .selected = false,
    });
    for (const TWPartition* partition : partitions_) {
      if (partition->wipe_available_in_gui_ && !partition->is_sub_partition_ && partition->
          is_present_) {
        partition_list->push_back({
          .display_name = partition->display_name_,
          .mount_point = partition->mount_point_,
          .selected = false,
        });
      }
      if (partition->has_android_secure_) {
        partition_list->push_back({
          .display_name = partition->backup_display_name_,
          .mount_point = partition->backup_path_,
          .selected = false,
        });
      }
      if (partition->has_data_media_) {
        partition_list->push_back({
          .display_name = partition->storage_name_,
          .mount_point = "INTERNAL",
          .selected = false,
        });
      }
    }
  } else if (list_type == "flashimg") {
    for (const TWPartition* partition : partitions_) {
      if (partition->can_flash_img_ && partition->is_present_) {
        partition_list->push_back({
          .display_name = partition->backup_display_name_,
          .mount_point = partition->backup_path_,
          .selected = false,
        });
      }
    }
    if (DataManager::GetIntValue("tw_has_repack_tools") != 0 &&
        DataManager::GetIntValue("tw_has_boot_slots") != 0 &&
        DataManager::GetIntValue("tw_include_install_recovery_ramdisk") != 0) {
#ifdef BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT
      const std::string dest_partition = "/vendor_boot";
#else
      const std::string dest_partition = "/boot";
#endif

      if (FindPartitionByPath(dest_partition)) {
        // Allow flashing kernels and ramdisks
        partition_list->push_back({
          .display_name = gui_lookup("install_twrp_ramdisk", "Install Recovery Ramdisk"),
          .mount_point = "/repack_ramdisk",
          .selected = false,
        });
        LOGINFO("Install Recovery Ramdisk: target partition=%s\n", dest_partition.c_str());
      }
    }
  } else {
    LOGERR("Unknown list type '%s' requested for TWPartitionManager::GetPartitionList\n",
           list_type.c_str());
  }
}

bool TWPartitionManager::FstabProcessed() const {
  return !partitions_.empty();
}

void TWPartitionManager::OutputStorageFstab() const {
  if (TWFunc::GetLogDir().empty()) {
    LOGINFO("Unable to find cache directory\n");
    return;
  }

  const std::string storageFstab = TWFunc::GetLogDir() + "recovery/storage.fstab";
  std::ofstream fp(storageFstab);

  if (!fp.is_open()) {
    gui_msg(Msg(msg::kError, "unable_to_open=Unable to open '{1}'.")(storageFstab));
    return;
  }

  // Iterate through all partitions
  for (const TWPartition* partition : partitions_) {
    if (partition->is_storage_) {
      fp << partition->storage_path_ << ';' << partition->storage_name_ << ";\n";
    }
  }
}

TWPartition* TWPartitionManager::GetDefaultStoragePartition() const {
  TWPartition* res = nullptr;
  for (TWPartition* partition : partitions_) {
    if (!partition->is_storage_) continue;

    if (partition->is_settings_storage_) return partition;

    if (!res) res = partition;
  }
  return res;
}

bool TWPartitionManager::EnableMtp() {
#ifdef TW_HAS_MTP
  if (mtp_pid_) {
    gui_err("mtp_already_enabled=MTP already enabled");
    return true;
  }

  int mtppipe[2];

  if (pipe(mtppipe) < 0) {
    LOGERR("Error creating MTP pipe\n");
    return false;
  }

  if (android::base::GetProperty("sys.usb.config", "") != "mtp,adb") {
    android::base::SetProperty("sys.usb.config", "none");
    const std::string vendor = android::base::GetProperty("usb.vendor", "18D1");
    const std::string product = android::base::GetProperty("usb.product.mtpadb", "4EE2");
    TWFunc::WriteToFile("/sys/class/android_usb/android0/idVendor", vendor);
    TWFunc::WriteToFile("/sys/class/android_usb/android0/idProduct", product);
    android::base::SetProperty("sys.usb.config", "mtp,adb");
  }
  /* To enable MTP debug, use the twrp command line feature:
   * twrp set tw_mtp_debug 1
   */
  const auto mtp = std::make_unique<android::TwrpMtp>(DataManager::GetIntValue("tw_mtp_debug"));
  mtp_pid_ = mtp->ForkServer(mtppipe);
  if (mtp_pid_) {
    close(mtppipe[0]); // Host closes read side
    mtp_write_fd = mtppipe[1];
    DataManager::SetValue("tw_mtp_enabled", 1);
    AddAllMtpStorage();
    return true;
  }
  close(mtppipe[0]);
  close(mtppipe[1]);
  gui_err("mtp_fail=Failed to enable MTP");
  return false;
#else
  gui_err("no_mtp=MTP support not included");
#endif
  DataManager::SetValue("tw_mtp_enabled", 0);
  return false;
}

void TWPartitionManager::AddAllMtpStorage() const {
#ifdef TW_HAS_MTP
  if (!mtp_pid_) return; // MTP is not enabled

  for (TWPartition* partition : partitions_) {
    if (partition->is_storage_ && partition->is_present_ && partition->
        Mount(false))
      AddRemoveMtpStorage(partition, MTP_MESSAGE_ADD_STORAGE);
  }
#endif
}

bool TWPartitionManager::DisableMtp() {
  android::base::SetProperty("sys.usb.ffs.mtp.ready", "0");
  if (android::base::GetProperty("sys.usb.config", "") != "adb") {
    android::base::SetProperty("sys.usb.config", "none");
    const std::string vendor = android::base::GetProperty("usb.vendor", "18D1");
    const std::string product = android::base::GetProperty("usb.product.adb", "D001");
    TWFunc::WriteToFile("/sys/class/android_usb/android0/idVendor", vendor);
    TWFunc::WriteToFile("/sys/class/android_usb/android0/idProduct", product);
    usleep(2000);
  }
#ifdef TW_HAS_MTP
  if (mtp_pid_) {
    LOGINFO("Disabling MTP\n");
    int status;
    const pid_t pid = mtp_pid_;
    kill(pid, SIGKILL);
    mtp_pid_ = 0;
    // We don't care about the exit value, but this prevents a zombie process
    waitpid(pid, &status, 0);
    close(mtp_write_fd);
    mtp_write_fd = -1;
  }
#endif
  android::base::SetProperty("sys.usb.config", "adb");
#ifdef TW_HAS_MTP
  DataManager::SetValue("tw_mtp_enabled", 0);
  return true;
#endif
  return false;
}

TWPartition* TWPartitionManager::FindPartitionByMtpStorageId(const unsigned int storage_id) const {
  for (TWPartition* partition : partitions_) {
    if (partition->mtp_storage_id_ == storage_id) return partition;
  }
  return nullptr;
}

bool TWPartitionManager::AddRemoveMtpStorage(const TWPartition* part,
                                             const int message_type) const {
#ifdef TW_HAS_MTP
  MtpMessage mtp_message{};

  if (!mtp_pid_) return false; // MTP is disabled

  if (mtp_write_fd < 0) {
    LOGINFO("MTP: mtp_write_fd is not set\n");
    return false;
  }

  if (part) {
    if (part->mtp_storage_id_ == 0) return false;
    if (message_type == MTP_MESSAGE_REMOVE_STORAGE) {
      mtp_message.message_type = MTP_MESSAGE_REMOVE_STORAGE; // Remove
      LOGINFO("sending message to remove %i\n", part->mtp_storage_id_);
      mtp_message.storage_id = part->mtp_storage_id_;
      if (TEMP_FAILURE_RETRY(write(mtp_write_fd, &mtp_message, sizeof(mtp_message))) <= 0) {
        LOGINFO("error sending message to remove storage %i\n", part->mtp_storage_id_);
        return false;
      }
      LOGINFO("Message sent, remove storage ID: %i\n", part->mtp_storage_id_);
      return true;
    }
    if (message_type == MTP_MESSAGE_ADD_STORAGE && part->IsMounted()) {
      mtp_message.message_type = MTP_MESSAGE_ADD_STORAGE; // Add
      mtp_message.storage_id = part->mtp_storage_id_;
      if (part->storage_path_.size() >= sizeof(mtp_message.path)) {
        LOGERR("Storage path '%s' too large for mtpmsg\n", part->storage_path_.c_str());
        return false;
      }
      strcpy(mtp_message.path, part->storage_path_.c_str());
      if (part->storage_name_.size() >= sizeof(mtp_message.display)) {
        LOGERR("Storage name '%s' too large for mtpmsg\n", part->storage_name_.c_str());
        return false;
      }
      strcpy(mtp_message.display, part->storage_name_.c_str());
      mtp_message.max_file_size = part->current_file_system_ == "vfat" ? 0xFFFFFFFF : 0;
      LOGINFO("sending message to add %i '%s' '%s'\n", mtp_message.storage_id, mtp_message.path,
              mtp_message.display);
      if (TEMP_FAILURE_RETRY(write(mtp_write_fd, &mtp_message, sizeof(mtp_message))) <= 0) {
        LOGINFO("error sending message to add storage %i\n", part->mtp_storage_id_);
        return false;
      }
      LOGINFO("Message sent, add storage ID: %i '%s'\n", part->mtp_storage_id_, mtp_message.path);
      return true;
    }
    LOGERR("Unknown MTP message type: %i\n", message_type);
  } else {
    // This hopefully never happens as the error handling should
    // occur in the calling function.
    LOGINFO("TWPartitionManager::AddRemoveMtpStorage NULL partition given\n");
  }
  return true;
#else
  gui_err("no_mtp=MTP support not included");
  DataManager::SetValue("tw_mtp_enabled", 0);
  return false;
#endif
}

bool TWPartitionManager::AddMtpStorage(const std::string& mount_point) {
#ifdef TW_HAS_MTP
  if (TWPartition* part = PartitionManager.FindPartitionByPath(mount_point)) {
    return PartitionManager.AddRemoveMtpStorage(part, MTP_MESSAGE_ADD_STORAGE);
  }
  LOGINFO("TWFunc::AddMtpStorage unable to locate partition for '%s'\n", mount_point.c_str());
#endif
  return false;
}

bool TWPartitionManager::AddMtpStorage(const unsigned int storage_id) {
#ifdef TW_HAS_MTP
  if (TWPartition* part = PartitionManager.FindPartitionByMtpStorageId(storage_id)) {
    return PartitionManager.AddRemoveMtpStorage(part, MTP_MESSAGE_ADD_STORAGE);
  }
  LOGINFO("TWFunc::AddMtpStorage unable to locate partition for %i\n", storage_id);
#endif
  return false;
}

bool TWPartitionManager::RemoveMtpStorage(const std::string& mount_point) {
#ifdef TW_HAS_MTP
  if (TWPartition* part = PartitionManager.FindPartitionByPath(mount_point)) {
    return PartitionManager.AddRemoveMtpStorage(part, MTP_MESSAGE_REMOVE_STORAGE);
  }
  LOGINFO("TWFunc::RemoveMtpStorage unable to locate partition for '%s'\n", mount_point.c_str());
#endif
  return false;
}

bool TWPartitionManager::RemoveMtpStorage(const unsigned int storage_id) {
#ifdef TW_HAS_MTP
  if (TWPartition* part = PartitionManager.FindPartitionByMtpStorageId(storage_id)) {
    return PartitionManager.AddRemoveMtpStorage(part, MTP_MESSAGE_REMOVE_STORAGE);
  }
  LOGINFO("TWFunc::RemoveMtpStorage unable to locate partition for %i\n", storage_id);
#endif
  return false;
}

bool TWPartitionManager::FlashImage(const std::string& path, const std::string& filename) const {
  int partition_count = 0;
  TWPartition* flash_part = nullptr;
  std::string flash_list;

  const std::string full_filename = fs::path(path) / filename;

  gui_msg("image_flash_start=[IMAGE FLASH STARTED]");
  gui_msg(Msg("img_to_flash=Image to flash: '{1}'")(full_filename));

  if (!TWFunc::IsPathExists(full_filename)) {
    if (!MountByPath(full_filename, true)) {
      return false;
    }
    if (!TWFunc::IsPathExists(full_filename)) {
      gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")(full_filename));
      return false;
    }
  }

  DataManager::GetValue("tw_flash_partition", flash_list);
  Repack_Type repack = REPLACE_NONE;
  if (flash_list == "/repack_ramdisk;") {
    repack = REPLACE_RAMDISK;
  } else if (flash_list == "/repack_kernel;") {
    repack = REPLACE_KERNEL;
  }
  if (repack != REPLACE_NONE) {
    twrpRepacker repacker;
    const RepackOptionsStruct repack_options{
      .Type = repack,
      .Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0,
      .Disable_Verity = false,
      .Disable_Force_Encrypt = false,
    };
    return repacker.Repack_Image_And_Flash(full_filename, repack_options);
  }
  PartitionSettings part_settings;
  part_settings.backup_folder = path;
  const uint64_t total_bytes = TWFunc::GetFileSize(full_filename);
  ProgressTracking progress(total_bytes);
  part_settings.progress = &progress;
  part_settings.pm_method = PartitionManagerOp::kRestore;
  gui_msg("calc_restore=Calculating restore details...");
  if (!flash_list.empty()) {
    size_t end_pos = 0;
    size_t start_pos = 0;
    end_pos = flash_list.find(';', start_pos);
    while (end_pos != std::string::npos && start_pos < flash_list.size()) {
      std::string flash_path = flash_list.substr(start_pos, end_pos - start_pos);
      flash_part = FindPartitionByPath(flash_path);
      if (flash_part) {
        partition_count++;
        if (partition_count > 1) {
          gui_err("too_many_flash=Too many partitions selected for flashing.");
          return false;
        }
      } else {
        gui_msg(
            Msg(msg::kError, "flash_unable_locate=Unable to locate '{1}' partition for flashing.")(
                flash_path));
        return false;
      }
      start_pos = end_pos + 1;
      end_pos = flash_list.find(';', start_pos);
    }
  }

  if (partition_count == 0) {
    gui_err("no_part_flash=No partitions selected for flashing.");
    return false;
  }

  DataManager::SetProgress(0.0);
  if (flash_part) {
    flash_part->backup_file_name_ = filename;
    if (!flash_part->FlashImage(&part_settings)) return false;
  } else {
    gui_err("invalid_flash=Invalid flash partition specified.");
    return false;
  }
  gui_highlight("flash_done=IMAGE FLASH COMPLETED]");
  return true;
}

void TWPartitionManager::TranslatePartition(const std::string& path,
                                            const std::string& resource_name,
                                            const std::string& default_value) {
  if (TWPartition* part = PartitionManager.FindPartitionByPath(path)) {
    if (part->is_adopted_storage_) {
      part->display_name_ = std::format("{} - {}", part->display_name_, gui_lookup("data", "Data"));
      part->backup_display_name_ = part->display_name_;
      part->storage_name_ = std::format("{} - {}", part->storage_name_,
                                        gui_lookup("adopted_storage", "Adopted Storage"));
    } else {
      part->display_name_ = gui_lookup(resource_name, default_value);
      part->backup_display_name_ = part->display_name_;
    }
  }
}

void TWPartitionManager::TranslatePartition(const std::string& path,
                                            const std::string& resource_name,
                                            const std::string& default_value,
                                            const std::string& storage_resource_name,
                                            const std::string& storage_default_value) {
  if (TWPartition* part = PartitionManager.FindPartitionByPath(path)) {
    if (part->is_adopted_storage_) {
      part->backup_display_name_ = std::format("{} - {}", part->display_name_,
                                               gui_lookup("data_backup", "Data (excl. storage)"));
      part->display_name_ = std::format("{} - {}", part->display_name_, gui_lookup("data", "Data"));
      part->storage_name_ = std::format("{} - {}", part->storage_name_,
                                        gui_lookup("adopted_storage", "Adopted Storage"));
    } else {
      part->display_name_ = gui_lookup(resource_name, default_value);
      part->backup_display_name_ = part->display_name_;
      if (part->is_storage_)
        part->storage_name_ = gui_lookup(storage_resource_name,
                                         storage_default_value);
    }
  }
}

void TWPartitionManager::TranslatePartition(const std::string& path,
                                            const std::string& resource_name,
                                            const std::string& default_value,
                                            const std::string& storage_resource_name,
                                            const std::string& storage_default_value,
                                            const std::string& backup_name,
                                            const std::string& backup_default) {
  if (TWPartition* part = PartitionManager.FindPartitionByPath(path)) {
    if (part->is_adopted_storage_) {
      part->backup_display_name_ = std::format("{} - {}", part->display_name_,
                                               gui_lookup(backup_name, backup_default));
      part->display_name_ = std::format("{} - {}", part->display_name_, gui_lookup("data", "Data"));
      part->storage_name_ = std::format("{} - {}", part->storage_name_,
                                        gui_lookup("adopted_storage", "Adopted Storage"));
    } else {
      part->display_name_ = gui_lookup(resource_name, default_value);
      part->backup_display_name_ = gui_lookup(backup_name, backup_default);
      if (part->is_storage_)
        part->storage_name_ = gui_lookup(
            storage_resource_name, storage_default_value);
    }
  }
}

void TWPartitionManager::TranslatePartitionDisplayNames() const {
  LOGINFO("Translating partition display names\n");
  TranslatePartition("/system", "system", "System");
  TranslatePartition("/system_image", "system_image", "System Image");
  TranslatePartition("/vendor", "vendor", "Vendor");
  TranslatePartition("/vendor_image", "vendor_image", "Vendor Image");
  TranslatePartition("/cache", "cache", "Cache");
  TranslatePartition("/boot", "boot", "Boot");
  TranslatePartition("/recovery", "recovery", "Recovery");
  if (!datamedia) {
    TranslatePartition("/data", "data", "Data", "internal", "Internal Storage");
    TranslatePartition("/sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
    TranslatePartition("/internal_sd", "sdcard", "SDCard", "sdcard", "SDCard");
    TranslatePartition("/internal_sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
    TranslatePartition("/emmc", "sdcard", "SDCard", "sdcard", "SDCard");
  } else {
    TranslatePartition("/data", "data", "Data", "internal", "Internal Storage", "data_backup",
                       "Data (excl. storage)");
  }
  TranslatePartition("/external_sd", "microsd", "Micro SDCard", "microsd", "Micro SDCard",
                     "data_backup",
                     "Data (excl. storage)");
  TranslatePartition("/external_sdcard", "microsd", "Micro SDCard", "microsd", "Micro SDCard",
                     "data_backup",
                     "Data (excl. storage)");
  TranslatePartition("/usb-otg", "usbotg", "USB OTG", "usbotg", "USB OTG");
  TranslatePartition("/sd-ext", "sdext", "SD-EXT");

  // Android secure is a special case
  TWPartition* part = PartitionManager.FindPartitionByPath("/and-sec");
  if (part) part->backup_display_name_ = gui_lookup("android_secure", "Android Secure");

  // So is super, its name carries the count SetupSuperPartition() found
  part = PartitionManager.FindPartitionByPath("/super");
  if (part)
    part->backup_display_name_ = std::format("Super ({0} {1})", super_partition_list_.size(),
                                             gui_lookup("partitions", "partitions"));

  for (const TWPartition* partition : partitions_) {
    if (!partition->sysfs_entry_.empty()) {
      TranslatePartition(partition->mount_point_, "autostorage", "Storage", "autostorage",
                         "Storage");
    }
  }

  // This updates the text on all of the storage selection buttons in the GUI
  DataManager::SetBackupFolder();
}

void TWPartitionManager::RemovePartitionByPath(const std::string& Path) {
  const std::string local_path = TWFunc::GetRootPath(Path);

  // First-match removal: erase the first partition whose mount_point_ (or
  // symlink_mount_point_) equals local_path, then stop. No `delete` here —
  // ownership lives elsewhere (contrast RemoveUeventDevices /
  // UnmapSuperDevices, which delete and so stay manual erase loops).
  const auto iter = std::ranges::find_if(
      partitions_, [&](const TWPartition* partition) {
        return MatchesPath(partition, local_path);
      });
  if (iter != partitions_.end()) {
    LOGINFO("Found and erasing '%s' from partition list\n", local_path.c_str());
    partitions_.erase(iter);
  }
}

void TWPartitionManager::OverrideActiveSlot(const std::string& slot) {
  LOGINFO("Overriding slot to '%s'\n", slot.c_str());
  active_slot_display_ = slot;
  DataManager::SetValue("tw_active_slot", slot);
  PartitionManager.UpdateSystemDetails();
}

void TWPartitionManager::SetActiveSlot(const std::string& slot) {
  if (slot != "A" && slot != "B") {
    LOGERR("SetActiveSlot invalid slot '%s'\n", slot.c_str());
    return;
  }
  if (active_slot_display_ == slot) return;
  LOGINFO("Setting active slot %s\n", slot.c_str());
#ifdef AB_OTA_UPDATER
  if (!active_slot_display_.empty()) {
    if (const auto module = BootControlClient::WaitForService(); !module) {
      LOGERR("Error getting bootctrl module.\n");
    } else {
      int32_t slot_number = 0;
      if (slot == "B") slot_number = 1;
      if (const auto [success, errMsg] = module->SetActiveBootSlot(slot_number); !success) {
        gui_msg(
            Msg(msg::kError,
                "unable_set_boot_slot=Error changing bootloader boot slot to {1}")(slot));
      }
    }
    DataManager::SetValue("tw_active_slot", slot);
    // Doing this outside of this if block may result in a seg fault because the DataManager may not be ready yet
  }
#else
  LOGERR("Boot slot feature not present\n");
#endif
  active_slot_display_ = slot;
  if (FstabProcessed()) UpdateSystemDetails();
}

std::string TWPartitionManager::GetActiveSlotSuffix() const {
  if (active_slot_display_ == "A") return "_a";
  return "_b";
}

std::string TWPartitionManager::GetActiveSlotDisplay() {
  return active_slot_display_;
}

std::string TWPartitionManager::GetAndroidRootPath() {
  return "/system_root";
}

void TWPartitionManager::RemoveUeventDevices(const std::string& sysfs_path) {
  for (auto iter = partitions_.begin(); iter != partitions_.end();) {
    if ((*iter)->is_sub_partition_ && (*iter)->sub_partition_of_ == sysfs_path) {
      const TWPartition* part = *iter;
      LOGINFO("%s was removed by uevent data\n", (*iter)->mount_point_.c_str());
      (*iter)->Unmount(false);
      rmdir((*iter)->mount_point_.c_str());
      iter = partitions_.erase(iter);
      delete part;
    } else {
      ++iter;
    }
  }
}

void TWPartitionManager::HandleUevent(const UeventBlockData& uevent_data) {
  for (TWPartition* partition : partitions_) {
    if (!partition->sysfs_entry_.empty()) {
      std::string device;
      if (const size_t wildcard = partition->sysfs_entry_.find('*');
        wildcard != std::string::npos) {
        device = partition->sysfs_entry_.substr(0, wildcard);
      } else {
        device = partition->sysfs_entry_;
      }
      if (device == uevent_data.sysfs_path.substr(0, device.size())) {
        // Found a match
        if (uevent_data.type == "partition") {
          // The disk's add event can arrive before its partitions.
          // Rescan using the disk path, not the new partition as a
          // wildcard prefix (sda1* would miss sda2).
          if (uevent_data.action == "add" || uevent_data.action == "change") {
            partition->FindActualBlockDevice();
          } else if (uevent_data.action == "remove") {
            const std::string block = fs::path("/dev/block") / uevent_data.block_device;
            for (auto it = partitions_.begin(); it != partitions_.end(); ++it) {
              TWPartition* child = *it;
              if (child->is_sub_partition_ && child->sub_partition_of_ == partition->mount_point_ &&
                  child->primary_block_device_ == block) {
                child->Unmount(false);
                rmdir(child->mount_point_.c_str());
                partitions_.erase(it);
                delete child;
                break;
              }
            }
          }
          return;
        }
        if (uevent_data.action == "add") {
          partition->primary_block_device_ = fs::path("/dev/block") / uevent_data.block_device;
          partition->alternate_block_device_ = partition->primary_block_device_;
          partition->is_present_ = true;
          LOGINFO("Found a match '%s' '%s'\n", uevent_data.block_device.c_str(), device.c_str());
          partition->FindActualBlockDevice();
          return;
        }
        if (uevent_data.action == "remove") {
          partition->is_present_ = false;
          partition->primary_block_device_ = "";
          partition->actual_block_device_ = "";
          RemoveUeventDevices(partition->mount_point_);
          return;
        }
      }
    }
  }

  if (!GetSuperStatus())
    LOGINFO("Found no matching fstab entry for uevent device '%s' - %s\n",
          uevent_data.sysfs_path.c_str(),
          uevent_data.action.c_str());
}

void TWPartitionManager::SetupUevent() {
  if (uevent_pfd.fd >= 0) {
    LOGINFO("uevent already set up\n");
    return;
  }

  // Open hotplug event netlink socket
  sockaddr_nl nls = {
    .nl_family = AF_NETLINK,
    .nl_pid = static_cast<__u32>(getpid()),
    .nl_groups = -1u
  };
  uevent_pfd.events = POLLIN;
  uevent_pfd.fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
  if (uevent_pfd.fd == -1) {
    LOGERR("uevent not root\n");
    return;
  }

  // Listen to netlink socket
  if (::bind(uevent_pfd.fd, reinterpret_cast<sockaddr*>(&nls), sizeof(sockaddr_nl)) < 0) {
    LOGERR("Bind failed\n");
    return;
  }
  set_select_fd();
  Coldboot();
}

UeventBlockData TWPartitionManager::GetEventBlockValues(char* buf, const int len) {
  UeventBlockData ret;
  ret.subsystem = "";
  const char* ptr = buf;
  const char* end = buf + len;

  buf[len - 1] = '\0';
  while (ptr < end) {
    // The buffer is a netlink uevent: NUL-separated "KEY=VALUE" records, not one string.
    // `field` is the current record (string_view stops at its terminating NUL, exactly
    // bounding what strncmp over the same record would compare).
    if (std::string_view field(ptr); field.starts_with("ACTION=")) {
      ptr += strlen("ACTION=");
      ret.action = ptr;
    } else if (field.starts_with("SUBSYSTEM=")) {
      ptr += strlen("SUBSYSTEM=");
      ret.subsystem = ptr;
    } else if (field.starts_with("DEVTYPE=")) {
      ptr += strlen("DEVTYPE=");
      ret.type = ptr;
    } else if (field.starts_with("DEVPATH=")) {
      ptr += strlen("DEVPATH=");
      ret.sysfs_path += ptr;
    } else if (field.starts_with("DEVNAME=")) {
      ptr += strlen("DEVNAME=");
      ret.block_device += ptr;
    }
    ptr += strlen(ptr) + 1;
  }
  return ret;
}

void TWPartitionManager::ReadUevent() const {
  // USB discovery emits many unrelated events. Drain a bounded batch so a
  // partition is not delayed by one event per rendered frame.
  for (int count = 0; count < 64; ++count) {
    char buf[4096];
    const auto len = recv(uevent_pfd.fd, buf, sizeof(buf), MSG_DONTWAIT | MSG_TRUNC);
    if (len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        LOGINFO("recv error on uevent: %s\n", strerror(errno));
      return;
    }
    if (len == 0 || len >= static_cast<decltype(len)>(sizeof(buf))) continue;
    const UeventBlockData uevent_data = GetEventBlockValues(buf, static_cast<int>(len));
    if (uevent_data.subsystem == "block" &&
        (uevent_data.type == "disk" || uevent_data.type == "partition")) {
      PartitionManager.HandleUevent(uevent_data);
    }
  }
}

void TWPartitionManager::CloseUevent() {
  if (uevent_pfd.fd >= 0) close(uevent_pfd.fd);
  uevent_pfd.fd = -1;
}

void TWPartitionManager::AddPartition(TWPartition* part) {
  partitions_.push_back(part);
}

void TWPartitionManager::ColdbootScan(std::span<const std::string> sysfs_entries,
                                      const fs::path& path, const int depth) {
  // Only trigger when the path resolves; the original ran the match inside the
  // realpath() success branch only.
  std::error_code ec;
  if (const fs::path resolved = fs::canonical(path, ec); !ec) {
    const auto write_path = resolved / "uevent";
    const auto real = resolved.string();
    if (TWFunc::IsPathExists(write_path) &&
        std::ranges::any_of(sysfs_entries, [&](const std::string& e) {
          return real.find(e) != std::string::npos;
        })) {
      TWFunc::WriteToFile(write_path, "add\n");
    }
  }

  // Recurse. directory_iterator follows symlinks at the top level (like opendir);
  // symlink_status() reports the entry's own type, matching DT_DIR d_type semantics.
  for (std::error_code dec; const auto& entry : fs::directory_iterator(path, dec)) {
    auto name = entry.path().filename().string();
    if (name.empty() || name.starts_with('.')) continue;
    if (fs::symlink_status(entry.path(), dec).type() != fs::file_type::directory && depth >
        0)
      continue;
    if (name.starts_with("ram") || name.starts_with("loop")) continue;
    ColdbootScan(sysfs_entries, entry.path(), depth + 1);
  }
}

void TWPartitionManager::Coldboot() const {
  std::vector<std::string> sysfs_entries;
  for (TWPartition* partition : partitions_)
    if (!partition->sysfs_entry_.empty())
      sysfs_entries.push_back(
          partition->sysfs_entry_.substr(0, partition->sysfs_entry_.find('*')));

  if (!sysfs_entries.empty()) ColdbootScan(sysfs_entries, "/sys/block", 0);
}

bool TWPartitionManager::PrepareEmptyFolder(const std::string& Folder) {
  if (TWFunc::IsPathExists(Folder)) TWFunc::RemoveDir(Folder, false);
  return TWFunc::RecursiveMkdir(Folder);
}

std::string TWPartitionManager::GetBarePartitionName(const std::string& mountpoint) {
  if (mountpoint == "/system_root") return "system";
  return TWFunc::RemoveBeginningSlash(mountpoint);
}

static std::string GetByNamePath(const std::string& partition_name) {
  for (const char* dir : {"/dev/block/bootdevice/by-name", "/dev/block/by-name"}) {
    const fs::path path = fs::path(dir) / partition_name;
    if (access(path.c_str(), F_OK) == 0) return path;
  }
  return fs::path("/dev/block/by-name") / partition_name;
}

bool TWPartitionManager::PrepareSuperVolume(TWPartition* twrpPart) {
  Fstab fstab;
  const std::string bare_partition_name = GetBarePartitionName(twrpPart->GetMountPoint());

  super_partition_list_.push_back(bare_partition_name);
  LOGINFO("Trying to prepare %s from super partition\n", bare_partition_name.c_str());

  std::string blk_device_partition;
#ifdef AB_OTA_UPDATER
  blk_device_partition = bare_partition_name + PartitionManager.GetActiveSlotSuffix();
#else
  blk_device_partition = bare_partition_name;
#endif

  FstabEntry fstab_entry = {
    .blk_device = blk_device_partition,
    .mount_point = twrpPart->GetMountPoint(),
    .fs_type = twrpPart->current_file_system_,
    .fs_mgr_flags = {
      .logical = twrpPart->is_super_
    },
  };

  fstab.emplace_back(fstab_entry);
  if (!fs_mgr_update_logical_partition(&fstab_entry)) {
    LOGINFO("unable to update logical partition: %s\n", twrpPart->GetMountPoint().c_str());
    return false;
  }

  while (access(fstab_entry.blk_device.c_str(), F_OK) != 0) {
    usleep(100);
  }

  twrpPart->SetBlockDevice(fstab_entry.blk_device);
  // The inactive slot was just updated. Its filesystems may not be mountable
  // until recovery is restarted, so do not report this expected probe failure.
  twrpPart->UpdateSize(false);
  twrpPart->SetCanBeBackedUp(false);
  twrpPart->SetCanBeWiped(false);
  const std::string bare_partition = GetByNamePath(bare_partition_name);

  if (access(bare_partition.c_str(), F_OK) == -1) {
    LOGINFO("Symlinking %s => %s \n", fstab_entry.blk_device.c_str(), bare_partition.c_str());
    symlink(fstab_entry.blk_device.c_str(), bare_partition.c_str());
    android::base::SetProperty("twrp.super.symlinks_created", "true");
  }

  return true;
}

bool TWPartitionManager::PrepareAllSuperVolumes() {
  bool status = true;

  // Snapshot super partition pointers: iterating a separate container avoids
  // iterator invalidation from erasing while looping over partitions_.
  std::vector<TWPartition*> supers;
  for (TWPartition* p : partitions_) if (p->is_super_) supers.push_back(p);

  for (TWPartition* part : supers) {
    if (PrepareSuperVolume(part)) {
      OutputPartition(part); // only on success; the old loop printed the
      // previous partition on failure
    } else {
      status = false;
      std::erase(partitions_, part); // remove by pointer value; the vector never owned the object
    }
  }

  UpdateSystemDetails(false, false);
  return status;
}

std::string TWPartitionManager::GetSuperPartition() {
  std::string super_device = "super";
  if (const auto module = BootControlClient::WaitForService()) {
    super_device = fs_mgr_get_super_partition_name(module->GetCurrentSlot());
  } else {
    // No boot control HAL is available.
    LOGINFO("Boot control HAL unavailable, using static super partition path\n");
  }
  return GetByNamePath(super_device);
}

void TWPartitionManager::SetupSuperDevices() {
  const std::string superPart = GetSuperPartition();
  android::fs_mgr::CreateLogicalPartitions(superPart);
}

void TWPartitionManager::SetupSuperPartition() {
  const std::string superPart = GetSuperPartition();
  auto superPartition = std::make_unique<TWPartition>();

  superPartition->backup_path_ = "/super";
  superPartition->mount_point_ = "/super";
  superPartition->actual_block_device_ = superPart;
  superPartition->alternate_block_device_ = superPart;
  // Translated in TranslatePartitionDisplayNames(), no resources yet.
  superPartition->backup_display_name_ = std::format("Super ({}) partitions",
                                                     super_partition_list_.size());
  superPartition->can_flash_img_ = true;
  superPartition->current_file_system_ = "emmc";
  superPartition->can_be_backed_up_ = true;
  superPartition->is_present_ = true;
  superPartition->is_sub_partition_ = false;
  superPartition->SetupImage();
  OutputPartition(superPartition.get());
  AddPartition(superPartition.release());
}

bool TWPartitionManager::GetSuperStatus() {
  return access(GetSuperPartition().c_str(), F_OK) == 0;
}

bool TWPartitionManager::RecreateLogsDir() {
#ifdef TW_INCLUDE_FBE
  auto resolve_uid = [](const char* name) -> std::optional<uid_t> {
    passwd pd{};
    passwd* result = nullptr;
    char buf[512];
    if (getpwnam_r(name, &pd, buf, sizeof(buf), &result) != 0 || !result) return std::nullopt;
    return pd.pw_uid;
  };
  auto resolve_gid = [](const char* name) -> std::optional<gid_t> {
    group grp{};
    group* result = nullptr;
    if (char buf[512]; getgrnam_r(name, &grp, buf, sizeof(buf), &result) != 0 || !result)
      return
          std::nullopt;
    return grp.gr_gid;
  };

  const auto uid = resolve_uid("system");
  if (!uid) {
    LOGERR("unable to get system user id\n");
    return false;
  }
  const auto gid = resolve_gid("cache");
  if (!gid) {
    LOGERR("unable to get cache group id\n");
    return false;
  }

  const std::string logs_recovery_dir = (fs::path(DATA_LOGS_DIR) / "recovery").string();
  if (!TWFunc::CreateDirRecursive(logs_recovery_dir, S_IRWXU | S_IRWXG, *uid, *gid)) {
    LOGERR("Unable to recreate %s\n", logs_recovery_dir.c_str());
    return false;
  }
  if (setfilecon(logs_recovery_dir.c_str(), "u:object_r:cache_file:s0") != 0) {
    LOGERR("Unable to set contexts for %s\n", logs_recovery_dir.c_str());
    return false;
  }
#endif
  return true;
}

void TWPartitionManager::UnlockBlockPartitions() {
  int OFF = 0;
  for (std::error_code ec; const auto& entry : fs::directory_iterator("/dev/block", ec)) {
    if (fs::symlink_status(entry.path(), ec).type() != fs::file_type::block) continue;
    const std::string block_device = entry.path().string();
    android::base::unique_fd fd(open(block_device.c_str(), O_RDONLY | O_CLOEXEC));
    if (fd.get() < 0) {
      LOGERR("unable to open block device %s: %s\n", block_device.c_str(), strerror(errno));
      continue;
    }
    if (ioctl(fd.get(), BLKROSET, &OFF) == -1) {
      LOGERR("Unable to unlock %s: %s\n", block_device.c_str(), strerror(errno));
    }
  }
}

// Destroys a logical partition only if its mapper node exists; ENOENT means it
// was never mapped (or is already gone), which counts as success.
static bool DestroyIfMapped(const std::string& name) {
  const auto mapper_path = fs::path("/dev/block/mapper") / name;
  struct stat st{};
  if (lstat(mapper_path.c_str(), &st) != 0) {
    if (errno == ENOENT) {
      LOGINFO("dynamic partition %s is already unmapped\n", name.c_str());
      return true;
    }
    LOGERR("Unable to inspect dynamic partition %s: %s\n", name.c_str(), strerror(errno));
    return false;
  }
  if (DestroyLogicalPartition(name)) return true;
  // It may have disappeared between lstat() and destruction.
  if (lstat(mapper_path.c_str(), &st) != 0 && errno == ENOENT) return true;
  return false;
}

bool TWPartitionManager::UnmapSuperDevices() {
#ifndef TW_EXCLUDE_APEX
  TwrpApex::Unmount();
#endif
  for (auto iter = partitions_.begin(); iter != partitions_.end();) {
    TWPartition* partition = *iter;
    LOGINFO("Checking partition: %s\n", partition->GetMountPoint().c_str());
    if (!partition->is_super_) {
      ++iter;
      continue;
    }

    std::string blk_device_partition = GetBarePartitionName(partition->GetMountPoint());
    if (DataManager::GetStrValue("tw_has_boot_slots") ==
        "1") blk_device_partition += GetActiveSlotSuffix();
    partition->Unmount(false);
    LOGINFO("removing dynamic partition: %s\n", blk_device_partition.c_str());
    bool destroyed = DestroyIfMapped(blk_device_partition);

    // Snapshot builds keep a <name>-cow mapper entry alongside the base one.
    const std::string cow_partition = std::format("{}-cow", blk_device_partition);
    const auto cow_partition_path = fs::path("/dev/block/mapper") / cow_partition;
    if (struct stat st{}; lstat(cow_partition_path.c_str(), &st) == 0) {
      LOGINFO("removing cow partition: %s\n", cow_partition.c_str());
      // Attempt both, then AND: a cow success must not mask a base failure.
      const bool cow_destroyed = DestroyIfMapped(cow_partition);
      destroyed = destroyed && cow_destroyed;
    }

    iter = partitions_.erase(iter);
    delete partition;
    if (!destroyed) return false;
  }

  // Sweep mapper entries with no fstab partition (e.g. leftovers from an
  // interrupted update).
  for (std::error_code ec; const auto& entry : fs::directory_iterator("/dev/block/mapper", ec)) {
    if (fs::symlink_status(entry.path(), ec).type() != fs::file_type::symlink) continue;
    const std::string partition = entry.path().filename();
    if (partition == "userdata") continue;
    LOGINFO("removing dynamic partition: %s\n", partition.c_str());
    if (!DestroyIfMapped(partition)) return false;
  }
  return true;
}


bool TWPartitionManager::CheckPendingMerges() {
  const auto sm = android::snapshot::SnapshotManager::NewForFirstStageMount();
  if (!sm) {
    LOGERR("Unable to call snapshot manager\n");
    return false;
  }

  if (!UnmapSuperDevices()) {
    LOGERR("Unable to unmap dynamic partitions.\n");
    return false;
  }

  auto callback = [&]() -> void {
    double progress;
    sm->GetUpdateState(&progress);
    LOGINFO("waiting for merge to complete: %.2f\n", progress);
  };

  LOGINFO("checking for merges\n");
  if (!sm->HandleImminentDataWipe(callback)) {
    LOGERR("Unable to check merge status\n");
    return false;
  }
  return true;
}