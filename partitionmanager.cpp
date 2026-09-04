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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <vector>
#include <span>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <ranges>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <fstream>
#include <format>
#include <sys/wait.h>
#include <linux/fs.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <fstab/fstab.h>
#include <fs_mgr.h>
#include <fs_mgr_dm_linear.h>
#include <liblp/liblp.h>
#include <liblp/builder.h>
#include <libsnapshot/snapshot.h>
#include <libavb_user/libavb_user.h>

#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "startupArgs.hpp"
#include "twrp-functions.hpp"
#include "fixContexts.hpp"
#include "exclude.hpp"
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "progresstracking.hpp"
#include "twrpDigestDriver.hpp"
#include "twrpRepacker.hpp"
#include "twrpadbbu/libtwrpadbbu.hpp"

#ifdef TW_LOAD_VENDOR_MODULES
#include "kernel_module_loader.hpp"
#endif

#ifdef TW_HAS_MTP
#include "mtp/ffs/MtpMessage.hpp"
#include "mtp/ffs/TwrpMtp.hpp"
#include "mtp/ffs/TwrpMtpServer.hpp"
#endif

extern "C" {
    #include "gui/gui.h"
}

#ifdef TW_INCLUDE_CRYPTO
#include "gui/rapidxml.hpp"
#include "gui/pages.hpp"
#ifdef TW_INCLUDE_FBE
#include "Decrypt.h"
#include "FsCrypt.h"
#ifdef TW_INCLUDE_FBE_METADATA_DECRYPT
#ifdef USE_FSCRYPT
#include "cryptfs.h"
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
std::vector<users_struct> Users_List;

std::string additional_fstab = "/etc/additional.fstab";

// 1 mebibyte; the bytes<->MiB unit conversion used throughout (e.g. static_cast<int>(bytes / kMiB)).
constexpr unsigned long long kMiB = 1024ULL * 1024;

TWPartitionManager::TWPartitionManager() {
    mtp_was_enabled = false;
    mtp_write_fd = -1;
    uevent_pfd.fd = -1;
    stop_backup = false;
#ifdef AB_OTA_UPDATER
    Active_Slot_Display = "";
    std::string slot = android::base::GetProperty("ro.boot.slot_suffix", "");
    Set_Active_Slot(slot == "_a" ? "A" : slot == "_b" ? "B" : "");
#endif
}

void TWPartitionManager::Set_Crypto_State() {
    if (android::base::GetProperty("ro.crypto.state", "error") == "error") {
        android::base::SetProperty("ro.crypto.state", "encrypted");
    }
}

int TWPartitionManager::Set_Crypto_Type(const std::string &crypto_type) {
    if (android::base::GetProperty("ro.crypto.type", "error") == "error") {
        android::base::SetProperty("ro.crypto.type", crypto_type);
        sleep(1);
    }
    return 0;
}

int override_prop(std::string prop, std::string mountpoint, std::string prop_file_name, std::string &prop_value) {
    std::string partition_prop = TWFunc::Partition_Property_Get(prop, PartitionManager, mountpoint, prop_file_name);
    if (partition_prop.empty()) return 1;

    if (TWFunc::Property_Override(prop, partition_prop) == NOT_AVAILABLE) {
        LOGERR("Unable to override '%s' due to missing libresetprop\n", prop.c_str());
    } else {
        prop_value = android::base::GetProperty(prop, "");
        LOGINFO("Setting '%s' to '%s' from %s/%s\n", prop.c_str(), prop_value.c_str(), mountpoint.c_str(),
                prop_file_name.c_str());
    }

    return 0;
}

void inline Reset_Prop_From_Partition(std::string prop, std::string def, TWPartition *ven, TWPartition *odm) {
    bool prop_on_odm = false, prop_on_vendor = false;
    std::string prop_value;
    if (odm && override_prop(prop, "/odm", "etc/build.prop", prop_value) == 0) {
        prop_on_odm = true;
    }
    if (ven && override_prop(prop, "/vendor", "build.prop", prop_value) == 0) {
        prop_on_vendor = true;
    }
    if (!prop_on_odm && !prop_on_vendor && !def.empty()) {
        if (TWFunc::Property_Override(prop, def) == NOT_AVAILABLE) {
            LOGERR("Unable to override '%s' due to missing libresetprop\n", prop.c_str());
        } else {
            prop_value = android::base::GetProperty(prop, "");
            LOGINFO("Setting '%s' to default value (%s)\n", prop.c_str(), prop_value.c_str());
        }
    }
    prop_value = android::base::GetProperty(prop, "");
    if (!prop_on_odm && !prop_on_vendor && !prop_value.empty() && def.empty()) {
        if (TWFunc::Delete_Property(prop) == NOT_AVAILABLE) {
            LOGERR("Unable to delete '%s' due to missing libresetprop\n", prop.c_str());
        } else {
            LOGINFO("Deleting property '%s'\n", prop.c_str());
        }
    }
}

#define AVB_MAGIC "AVB0"
#define AVB_MAGIC_LEN 4
#define AVB_VBMETA_FLAGS_OFFSET 123

static bool Do_Disable_AVB2(const std::string &ab_suffix,
                            bool disable_verity,
                            bool disable_verification,
                            bool Display_Info) {
    std::string vbmeta_part = "vbmeta";
    if (!ab_suffix.empty()) vbmeta_part += ab_suffix;
    AvbOps *ops = avb_ops_user_new();
    if (!ops) {
        if (Display_Info) gui_msg(
            Msg(msg::kError, "disable_avb2_fail_msg=Disable AVB2.0: processing '{1}' failed!")(vbmeta_part));
        return false;
    }
    bool ok = true;
    if (disable_verity) {
        if (!avb_user_verity_set(ops, ab_suffix.c_str(), false)) {
            ok = false;
            if (Display_Info)
                gui_msg(Msg(msg::kError,
                            "disable_avb2_fail_msg=Disable AVB2.0: processing '{1}' failed!")(vbmeta_part));
        }
    }
    if (disable_verification) {
        if (!avb_user_verification_set(ops, ab_suffix.c_str(), false)) {
            ok = false;
            if (Display_Info)
                gui_msg(Msg(msg::kError,
                            "disable_avb2_fail_msg=Disable AVB2.0: processing '{1}' failed!")(vbmeta_part));
        }
    }
    if (ok && Display_Info) {
        gui_msg(
            Msg(msg::kHighlight,
                "disable_avb2_success_msg=Disable AVB2.0: processing '{1}' successfully.")(vbmeta_part));
    }
    avb_ops_user_free(ops);
    return ok;
}

bool TWPartitionManager::Disable_AVB2(bool Display_Info) {
    bool disable_verity = true;
#ifdef TW_AVB_VBMETA_FLAGS_ALL_DISABLED
    bool disable_verification = true;
#else
    bool disable_verification = false;
#endif
#ifdef AB_OTA_UPDATER
    bool ok_a = Do_Disable_AVB2("_a", disable_verity, disable_verification, Display_Info);
    bool ok_b = Do_Disable_AVB2("_b", disable_verity, disable_verification, Display_Info);
    return ok_a && ok_b;
#else
    return Do_Disable_AVB2("", disable_verity, disable_verification, Display_Info);
#endif
}

void inline Process_ResetProps(TWPartition *ven, TWPartition *odm) {
    // Reset the crypto volume props according to os.
    Reset_Prop_From_Partition("ro.crypto.dm_default_key.options_format.version", "", ven, odm);
    Reset_Prop_From_Partition("ro.crypto.volume.metadata.method", "", ven, odm);
    Reset_Prop_From_Partition("ro.crypto.volume.options", "", ven, odm);
    Reset_Prop_From_Partition("external_storage.casefold.enabled", "", ven, odm);
    Reset_Prop_From_Partition("external_storage.sdcardfs.enabled", "", ven, odm);
}

static inline std::string KM_Ver_From_Manifest(std::string ver) {
    TWFunc::Get_Service_From_Manifest("/vendor", "android.hardware.keymaster", ver);
    if (ver.find('4') != std::string::npos) {
        ver = "4.x";
    }
    return ver;
}

void inline Process_Keymaster_Version(TWPartition *ven, bool Display_Error) {
    // Fetch the Keymaster Service version to be started
    std::string version;
#ifndef TW_FORCE_KEYMASTER_VER
    version = KM_Ver_From_Manifest(version);

    /* If we are unable to get the version from device vendor then
        * set the version from the keymaster_ver prop if set
        */
    if (version.empty()) {
        // unmount partition(s)
        if (ven) ven->UnMount(Display_Error);

        // Use keymaster_ver prop set from device tree (if exists)
        version = android::base::GetProperty(TW_KEYMASTER_VERSION_PROP, version);
        if (version.empty()) {
            LOGINFO(
                "Keymaster_Ver::Unable to find vendor manifest on the device, and no default value set. Checking the ramdisk manifest\n");
            version = KM_Ver_From_Manifest(version);
        } else {
            LOGINFO("Keymaster_Ver::Unable to find vendor manifest on the device. Setting to default value.\n");
        }
    } else {
        if (ven) ven->UnMount(Display_Error);
    }
#else
    if (ven) ven->UnMount(Display_Error);

    version = android::base::GetProperty(TW_KEYMASTER_VERSION_PROP, version);
    if (version.empty()) {
        LOGINFO("Keymaster_Ver::Force Keymaster_Ver flag found, but keymaster_ver prop not set.\n");
    } else {
        LOGINFO("Keymaster_Ver::Force Keymaster_Ver flag found.\n");
    }
#endif
    LOGINFO("Keymaster_Ver::Using keymaster version '%s' for decryption\n", version.c_str());
    android::base::SetProperty(TW_KEYMASTER_VERSION_PROP, version.c_str());
}

int TWPartitionManager::Process_Fstab(std::string Fstab_Filename, bool Display_Error, bool recovery_mode) {
    std::map<std::string, Flags_Map> twrp_flags;
    bool parse_userdata = false;

    if (Get_Super_Status()) {
        Setup_Super_Devices();
    }

    auto parse_twrp_flags = [&twrp_flags] {
        std::ifstream f("/etc/twrp.flags");
        if (!f) return;
        LOGINFO("Reading /etc/twrp.flags\n");
        std::string line;
        while (std::getline(f, line)) {
            Flags_Map lf;
            lf.fstab_line = line;
            std::vector<std::string> tokens;
            for (size_t i = 0; i < line.size();) {
                while (i < line.size() && line[i] <= 32)
                    i++;
                size_t start = i;
                while (i < line.size() && line[i] > 32)
                    i++;
                if (i > start)
                    tokens.push_back(line.substr(start, i - start));
            }
            if (!tokens.empty()) {
                lf.File_System = tokens[0];
                if (tokens.size() > 1)
                    lf.Primary_Block_Device = tokens[1];
                for (const auto &tok: tokens | std::views::drop(2)) {
                    if (tok[0] == '/')
                        lf.Alternate_Block_Device = tok;
                    else if (tok.size() > 6 && tok.starts_with("flags=")) {
                        lf.Flags = tok;
                        break;
                    }
                }
            }
            twrp_flags[tokens.empty() ? std::string{} : tokens[0]] = lf;
        }
    };
    parse_twrp_flags();

    TWPartition *data = nullptr, *meta = nullptr, *ven = nullptr, *odm = nullptr;

    auto parse_fstab_file = [&](const std::string &fname) -> bool {
        std::ifstream f(fname);
        if (!f && !parse_userdata) {
            LOGERR("Critical Error: Unable to open fstab at '%s'.\n", fname.c_str());
            return false;
        }
        LOGINFO("Reading %s\n", fname.c_str());
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("swap") != std::string::npos)
                continue; // Skip swap in recovery
            if (line.starts_with('#'))
                continue;
            if (parse_userdata) {
                if (line.find("/metadata") != std::string::npos && line.find("/data") == std::string::npos) {
                    if (meta) {
                        std::erase(Partitions, meta);
                        delete meta;
                        meta = nullptr;
                    }
                } else if (line.find("/data") != std::string::npos) {
                    if (data) {
                        std::erase(Partitions, data);
                        delete data;
                        data = nullptr;
                    }
                } else {
                    continue;
                }
            }

            TWPartition *partition = new TWPartition();
            bool keep = partition->Process_Fstab_Line(line.c_str(), Display_Error,
                                                      parse_userdata ? nullptr : &twrp_flags);
            if (keep) {
                if (partition->Mount_Point == "/data") data = partition;
                if (partition->Mount_Point == "/metadata") meta = partition;
                if (partition->Is_Super && !Prepare_Super_Volume(partition))
                    keep = false;
            }
            if (keep)
                Partitions.push_back(partition);
            else
                delete partition;
        }
        return true;
    };

    bool need_vendor_pass = false;
    do {
        if (!parse_fstab_file(Fstab_Filename))
            return false;

        if (!parse_userdata && !twrp_flags.empty()) {
            LOGINFO("Processing remaining twrp.flags\n");
            // Add any items from twrp.flags that did not exist in the recovery.fstab
            for (auto &[mount_point, line_flags]: twrp_flags) {
                if (Find_Partition_By_Path(mount_point) == nullptr) {
                    TWPartition *partition = new TWPartition();
                    if (partition->Process_Fstab_Line(line_flags.fstab_line.c_str(), Display_Error, nullptr))
                        Partitions.push_back(partition);
                    else
                        delete partition;
                }
            }
        }

#ifdef TW_LOAD_VENDOR_MODULES
        KernelModuleLoader::Load_Vendor_Modules();
#endif

        ven = PartitionManager.Find_Partition_By_Path("/vendor");
        odm = PartitionManager.Find_Partition_By_Path("/odm");

        need_vendor_pass = false;
        if (recovery_mode && !parse_userdata) {
            if (ven) ven->Mount(Display_Error);
            if (odm) odm->Mount(Display_Error);

            Process_ResetProps(ven, odm);
            parse_userdata = true;

#ifndef TW_SKIP_ADDITIONAL_FSTAB
            // Now Fetch the additional fstab
            if (TWFunc::Find_Fstab(Fstab_Filename)) {
                LOGINFO("Fstab: %s\n", Fstab_Filename.c_str());
                TWFunc::copy_file(Fstab_Filename, additional_fstab, 0600, false);
                Fstab_Filename = additional_fstab;
                android::base::SetProperty("fstab.additional", "1");
                need_vendor_pass = true;
            } else {
                LOGINFO("Unable to parse vendor fstab\n");
            }
        }
        if (!need_vendor_pass)
            LOGINFO("Done processing fstab files\n");
#else
        LOGINFO("Skipping Additional Fstab Processing\n");
        android::base::SetProperty("fstab.additional", "0");
        }
#endif
    } while (need_vendor_pass);

    if (odm) odm->UnMount(Display_Error);
    if (recovery_mode)
        Process_Keymaster_Version(ven, false);
    if (ven) ven->UnMount(Display_Error);

    return true;
}

void TWPartitionManager::Setup_Fstab_Partitions(bool Display_Error) {
    TWPartition *settings_partition = nullptr;
    TWPartition *andsec_partition = nullptr;
    unsigned int storageid = 1 << 16; // upper 16 bits are for physical storage device, we pretend to have only one

    for (TWPartition *partition: Partitions) {
        partition->Partition_Post_Processing(Display_Error);

        if (partition->Is_Storage) {
            ++storageid;
            partition->MTP_Storage_ID = storageid;
        }

        if (!settings_partition && partition->Is_Settings_Storage && partition->Is_Present)
            settings_partition = partition;
        else
            partition->Is_Settings_Storage = false;

        if (!andsec_partition && partition->Has_Android_Secure && partition->Is_Present)
            andsec_partition = partition;
        else
            partition->Has_Android_Secure = false;
    }

    Unlock_Block_Partitions();

    //Setup Apex before decryption
    TWPartition *sys = Find_Partition_By_Path(Get_Android_Root_Path());
    TWPartition *ven = Find_Partition_By_Path("/vendor");
    if (sys) {
        if (sys->Get_Super_Status()) {
            sys->Mount(Display_Error);
            if (ven) {
                ven->Mount(Display_Error);
            }
#ifdef TW_EXCLUDE_APEX
            LOGINFO("Apex is disabled in this build\n");
#else
            twrpApex apex;
            if (!apex.loadApexImages()) {
                LOGERR("Unable to load apex images from %s\n", APEX_DIR);
                android::base::SetProperty("twrp.apex.loaded", "false");
            } else {
                android::base::SetProperty("twrp.apex.loaded", "true");
            }
            TWFunc::check_and_run_script("/sbin/resyncapex.sh", "apex");
#endif
        }
    }
#ifndef USE_VENDOR_LIBS
    if (ven)
        ven->UnMount(Display_Error);
    if (sys)
        sys->UnMount(Display_Error);
#endif

    if (!datamedia && !settings_partition && !Find_Partition_By_Path("/sdcard") &&
        !Find_Partition_By_Path("/internal_sd") &&
        !Find_Partition_By_Path("/internal_sdcard") &&
        !Find_Partition_By_Path("/emmc")) {
        // Attempt to automatically identify /data/media emulated storage devices
        TWPartition *Dat = Find_Partition_By_Path("/data");
        if (Dat) {
            LOGINFO("Using automatic handling for /data/media emulated storage device.\n");
            datamedia = true;
            Dat->Setup_Data_Media();
            settings_partition = Dat;
            // Since /data was not considered a storage partition earlier, we still need to assign an MTP ID
            ++storageid;
            Dat->MTP_Storage_ID = storageid;
        }
    }
    if (!settings_partition) {
        for (TWPartition *partition: Partitions) {
            if (partition->Is_Storage) {
                settings_partition = partition;
                break;
            }
        }
        if (!settings_partition)
            LOGERR("Unable to locate storage partition for storing settings file.\n");
    }
    if (!Write_Fstab()) {
        if (Display_Error)
            LOGERR("Error creating fstab\n");
        else
            LOGINFO("Error creating fstab\n");
    }

    if (andsec_partition) {
        Setup_Android_Secure_Location(andsec_partition);
    } else if (settings_partition) {
        Setup_Android_Secure_Location(settings_partition);
    }
    if (settings_partition) {
        Setup_Settings_Storage_Partition(settings_partition);
    }

#ifdef TW_INCLUDE_CRYPTO
    DataManager::SetValue(TW_IS_ENCRYPTED, 1);
    Decrypt_Data();
#endif

    Update_System_Details(true);
    if (Get_Super_Status())
        Setup_Super_Partition();
    UnMount_Main_Partitions();
#ifdef AB_OTA_UPDATER
    DataManager::SetValue("tw_active_slot", Get_Active_Slot_Display());
#endif
    setup_uevent();
}

int TWPartitionManager::Write_Fstab() {
    std::ofstream fp("/etc/fstab");
    if (!fp) {
        LOGINFO("Can not open /etc/fstab.\n");
        return false;
    }
    for (TWPartition *partition: Partitions) {
        if (partition->Can_Be_Mounted) {
            fp << partition->Actual_Block_Device << ' ' << partition->Mount_Point << ' '
                    << partition->Current_File_System << (partition->Mount_Read_Only ? " ro " : " rw ")
                    << "0 0\n";
        }
        // Handle subpartition tracking
        if (partition->Is_SubPartition) {
            TWPartition *ParentPartition = Find_Partition_By_Path(partition->SubPartition_Of);
            if (ParentPartition)
                ParentPartition->Has_SubPartition = true;
            else
                LOGERR("Unable to locate parent partition '%s' of '%s'\n", partition->SubPartition_Of.c_str(),
                   partition->Mount_Point.c_str());
        }
    }
    return true;
}

void TWPartitionManager::Decrypt_Data() {
#ifdef TW_INCLUDE_CRYPTO
    TWPartition *data = Find_Partition_By_Path("/data");
    if (data && data->Is_Encrypted && !data->Is_Decrypted) {
        Set_Crypto_State();
        // Ensure the metadata key directory's partition is mounted before we use it.
        if (TWPartition *key_dir = Find_Partition_By_Path(data->Key_Directory); key_dir && !key_dir->Is_Mounted())
            Mount_By_Path(data->Key_Directory, false);
        if (!data->Key_Directory.empty()) {
            Set_Crypto_Type("file");
#ifdef TW_INCLUDE_FBE_METADATA_DECRYPT
#ifdef USE_FSCRYPT
            std::vector<std::string> user_devices;
            std::vector<bool> device_aliased;
            if (android::vold::fscrypt_mount_metadata_encrypted(
                    data->Actual_Block_Device, data->Mount_Point, false, false,
                    data->Current_File_System, false, user_devices, device_aliased, 0,
                    TWFunc::Path_Exists(additional_fstab) ? additional_fstab : "")) {
                    std::string crypto_blkdev = android::base::GetProperty("ro.crypto.fs_crypto_blkdev", "error");
                    data->Decrypted_Block_Device = crypto_blkdev;
                    LOGINFO("Successfully decrypted metadata encrypted data partition with new block device: '%s'\n",
                            crypto_blkdev.c_str());
#endif
                    data->Is_Decrypted = true; // Needed to make the mount function work correctly
                    int retry_count = 10;
                    while (!data->Mount(false) && --retry_count)
                        usleep(500);
                    if (data->Mount(false)) {
                        if (!data->Decrypt_FBE_DE())
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
        if (data->Is_FBE) {
            if (DataManager::GetIntValue(TW_CRYPTO_PWTYPE) == 0) {
                if (Decrypt_Device("!") == 0) {
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

void TWPartitionManager::Setup_Settings_Storage_Partition(TWPartition *Part) {
    DataManager::SetValue("tw_storage_path", Part->Storage_Path);
}

void TWPartitionManager::Setup_Android_Secure_Location(TWPartition *Part) {
    if (Part->Has_Android_Secure)
        Part->Setup_AndSec();
    else if (!datamedia)
        Part->Setup_AndSec();
}

void TWPartitionManager::Output_Partition_Logging() {
    printf("\n\nPartition Logs:\n");
    for (TWPartition *partition: Partitions) Output_Partition(partition);
}

void TWPartitionManager::Output_Partition(TWPartition *Part) {
    constexpr unsigned long long mb = 1048576;

    printf("%s | %s | Size: %iMB", Part->Mount_Point.c_str(), Part->Actual_Block_Device.c_str(),
           static_cast<int>(Part->Size / mb));
    if (Part->Can_Be_Mounted) {
        printf(" Used: %iMB Free: %iMB Backup Size: %iMB", static_cast<int>(Part->Used / mb),
               static_cast<int>(Part->Free / mb), static_cast<int>(Part->Backup_Size / mb));
    }
    struct FlagEntry {
        bool TWPartition::*flag;
        const char *name;
    };
    static constexpr FlagEntry kFlags[] = {
        {&TWPartition::Can_Be_Mounted, "Can_Be_Mounted"},
        {&TWPartition::Can_Be_Wiped, "Can_Be_Wiped"},
        {&TWPartition::Use_Rm_Rf, "Use_Rm_Rf"},
        {&TWPartition::Can_Be_Backed_Up, "Can_Be_Backed_Up"},
        {&TWPartition::Wipe_During_Factory_Reset, "Wipe_During_Factory_Reset"},
        {&TWPartition::Wipe_Available_in_GUI, "Wipe_Available_in_GUI"},
        {&TWPartition::Is_SubPartition, "Is_SubPartition"},
        {&TWPartition::Has_SubPartition, "Has_SubPartition"},
        {&TWPartition::Removable, "Removable"},
        {&TWPartition::Is_Present, "IsPresent"},
        {&TWPartition::Can_Be_Encrypted, "Can_Be_Encrypted"},
        {&TWPartition::Is_Encrypted, "Is_Encrypted"},
        {&TWPartition::Is_Decrypted, "Is_Decrypted"},
        {&TWPartition::Has_Data_Media, "Has_Data_Media"},
        {&TWPartition::Can_Encrypt_Backup, "Can_Encrypt_Backup"},
        {&TWPartition::Use_Userdata_Encryption, "Use_Userdata_Encryption"},
        {&TWPartition::Has_Android_Secure, "Has_Android_Secure"},
        {&TWPartition::Is_Storage, "Is_Storage"},
        {&TWPartition::Is_Settings_Storage, "Is_Settings_Storage"},
        {&TWPartition::Ignore_Blkid, "Ignore_Blkid"},
        {&TWPartition::Mount_To_Decrypt, "Mount_To_Decrypt"},
        {&TWPartition::Can_Flash_Img, "Can_Flash_Img"},
        {&TWPartition::Is_Adopted_Storage, "Is_Adopted_Storage"},
        {&TWPartition::SlotSelect, "SlotSelect"},
        {&TWPartition::Mount_Read_Only, "Mount_Read_Only"},
        {&TWPartition::Is_Super, "Is_Super"},
    };
    std::string flags;
    for (const auto &[f, n]: kFlags)
        if (Part->*f) {
            flags += n;
            flags += ' ';
        }
    printf("\n   Flags: %s\n", flags.c_str());
    if (!Part->SubPartition_Of.empty())
        printf("   SubPartition_Of: %s\n", Part->SubPartition_Of.c_str());
    if (!Part->Symlink_Path.empty())
        printf("   Symlink_Path: %s\n", Part->Symlink_Path.c_str());
    if (!Part->Symlink_Mount_Point.empty())
        printf("   Symlink_Mount_Point: %s\n", Part->Symlink_Mount_Point.c_str());
    if (!Part->Primary_Block_Device.empty())
        printf("   Primary_Block_Device: %s\n", Part->Primary_Block_Device.c_str());
    if (!Part->Alternate_Block_Device.empty())
        printf("   Alternate_Block_Device: %s\n", Part->Alternate_Block_Device.c_str());
    if (!Part->Decrypted_Block_Device.empty())
        printf("   Decrypted_Block_Device: %s\n", Part->Decrypted_Block_Device.c_str());
    if (Part->Length != 0)
        printf("   Length: %i\n", Part->Length);
    if (!Part->Display_Name.empty())
        printf("   Display_Name: %s\n", Part->Display_Name.c_str());
    if (!Part->Storage_Name.empty())
        printf("   Storage_Name: %s\n", Part->Storage_Name.c_str());
    if (!Part->Backup_Path.empty())
        printf("   Backup_Path: %s\n", Part->Backup_Path.c_str());
    if (!Part->Backup_Name.empty())
        printf("   Backup_Name: %s\n", Part->Backup_Name.c_str());
    if (!Part->Backup_Display_Name.empty())
        printf("   Backup_Display_Name: %s\n", Part->Backup_Display_Name.c_str());
    if (!Part->Backup_FileName.empty())
        printf("   Backup_FileName: %s\n", Part->Backup_FileName.c_str());
    if (!Part->Storage_Path.empty())
        printf("   Storage_Path: %s\n", Part->Storage_Path.c_str());
    if (!Part->Current_File_System.empty())
        printf("   Current_File_System: %s\n", Part->Current_File_System.c_str());
    if (!Part->Fstab_File_System.empty())
        printf("   Fstab_File_System: %s\n", Part->Fstab_File_System.c_str());
    if (Part->Format_Block_Size != 0)
        printf("   Format_Block_Size: %lu\n", Part->Format_Block_Size);
    printf("   Backup_Method: %s\n", Part->Backup_Method_By_Name().c_str());
    if (Part->Mount_Flags || !Part->Mount_Options.empty())
        printf("   Mount_Flags: %i, Mount_Options: %s\n", Part->Mount_Flags, Part->Mount_Options.c_str());
    if (Part->MTP_Storage_ID)
        printf("   MTP_Storage_ID: %i\n", Part->MTP_Storage_ID);
    if (!Part->Key_Directory.empty())
        printf("   Metadata Key Directory: %s\n", Part->Key_Directory.c_str());
    printf("\n");
}

int TWPartitionManager::Mount_By_Path(std::string Path, bool Display_Error) {
    int ret = false;
    bool found = false;
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    if (Local_Path == "/tmp" || Local_Path == "/")
        return true;

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path || (
                !partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path)) {
            ret = partition->Mount(Display_Error);
            found = true;
        } else if (partition->Is_SubPartition && partition->SubPartition_Of == Local_Path) {
            partition->Mount(Display_Error);
        }
    }
    if (found) {
        return ret;
    } else if (Display_Error) {
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
    }
    return false;
}

int TWPartitionManager::UnMount_By_Path(std::string Path, bool Display_Error, int flags) {
    int ret = false;
    bool found = false;
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path || (
                !partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path)) {
            ret = partition->UnMount(Display_Error, flags);
            found = true;
        } else if (partition->Is_SubPartition && partition->SubPartition_Of == Local_Path) {
            partition->UnMount(Display_Error, flags);
        }
    }
    if (found) {
        return ret;
    } else if (Display_Error) {
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
    } else {
        LOGINFO("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
    }
    return false;
}

int TWPartitionManager::Is_Mounted_By_Path(std::string Path) {
    TWPartition *Part = Find_Partition_By_Path(Path);

    if (Part)
        return Part->Is_Mounted();
    else
        LOGINFO("Is_Mounted: Unable to find partition for path '%s'\n", Path.c_str());
    return false;
}

int TWPartitionManager::Mount_Current_Storage(bool Display_Error) {
    std::string current_storage_path = DataManager::GetCurrentStoragePath();

    if (Mount_By_Path(current_storage_path, Display_Error)) {
        TWPartition *FreeStorage = Find_Partition_By_Path(current_storage_path);
        if (FreeStorage)
            DataManager::SetValue(TW_STORAGE_FREE_SIZE, static_cast<int>(FreeStorage->Free / kMiB));
        return true;
    }
    return false;
}

int TWPartitionManager::Mount_Settings_Storage(bool Display_Error) {
    return Mount_By_Path(DataManager::GetSettingsStoragePath(), Display_Error);
}

TWPartition *TWPartitionManager::Find_Partition_By_Path(const std::string &Path) {
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    if (Local_Path == "/system")
        Local_Path = Get_Android_Root_Path();
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path ||
            (!partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path))
            return partition;
    }
    return nullptr;
}

TWPartition *TWPartitionManager::Find_Partition_By_Block_Device(const std::string &Block_Device) {
    for (TWPartition *partition: Partitions) {
        if (partition->Primary_Block_Device == Block_Device ||
            (!partition->Actual_Block_Device.empty() && partition->Actual_Block_Device == Block_Device))
            return partition;
    }
    return nullptr;
}

int TWPartitionManager::Check_Backup_Name(const std::string &Backup_Name, bool Display_Error, bool Must_Be_Unique) {
    // Check the backup name to ensure that it is the correct size and contains only valid characters
    // and that a backup with that name doesn't already exist

    // Check size
    if (Backup_Name.size() > MAX_BACKUP_NAME_LEN) {
        if (Display_Error)
            gui_err("backup_name_len=Backup name is too long.");
        return -2;
    }

    // A "0" (zero) means to use the current timestamp for the backup name
    if (Backup_Name == "0")
        return 0;

    // Check each character. The original inclusive ASCII ranges (65-91, 97-123)
    // admitted '[' and '{' as the byte just past 'Z'/'z'; the explicit checks below
    // preserve that exact valid set: space, 0-9, A-Z, a-z, and -_.{}[]
    auto is_valid = [](char ch) {
        unsigned char c = static_cast<unsigned char>(ch);
        return c == ' '
               || (c >= '0' && c <= '9')
               || (c >= 'A' && c <= 'Z')
               || c == '[' || c == ']' || c == '_'
               || (c >= 'a' && c <= 'z')
               || c == '{' || c == '}' || c == '-' || c == '.';
    };
    for (char ch: Backup_Name) {
        if (!is_valid(ch)) {
            if (Display_Error)
                gui_msg(
                    Msg(msg::kError, "backup_name_invalid=Backup name '{1}' contains invalid character: '{1}'")(
                        Backup_Name)(ch));
            return -3;
        }
    }

    if (Must_Be_Unique) {
        // Check to make sure that a backup with this name doesn't already exist
        std::string Backup_Loc;
        DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
        if (TWFunc::Path_Exists(std::format("{}/{}", Backup_Loc, Backup_Name))) {
            if (Display_Error)
                gui_err("backup_name_exists=A backup with that name already exists!");
            return -4;
        }
        // Backup is unique
    }
    // No problems found
    return 0;
}

bool TWPartitionManager::Backup_Partition(PartitionSettings *part_settings) {
    if (part_settings->Part == nullptr)
        return true;

    std::string backup_log = part_settings->Backup_Folder + "/recovery.log";
    int use_compression;
    DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);

    TWFunc::SetPerformanceMode(true);
    time_t start = time(nullptr);

    // Back up a single partition (the main partition or one of its sub-partitions):
    // run the backup, sync, and (when enabled) generate its digest. part_settings->Part
    // is set to the partition being processed so the caller's view stays in sync. A
    // false return replaces the original 'goto backup_error' early-exit.
    auto backup_one = [&](TWPartition *part) -> bool {
        part_settings->Part = part;
        if (!part->Backup(part_settings, &tar_fork_pid))
            return false;
        sync();
        sync();
        std::string full = part_settings->Backup_Folder + "/" + part->Backup_FileName;
        if (!part_settings->adbbackup && part_settings->generate_digest) {
            if (!twrpDigestDriver::Make_Digest(full))
                return false;
        }
        return true;
    };

    bool ok = [&]() -> bool {
        TWPartition *parent = part_settings->Part;
        if (!backup_one(parent))
            return false;
        if (parent->Has_SubPartition) {
            for (TWPartition *subpart: Partitions) {
                if (subpart->Can_Be_Backed_Up && subpart->Is_SubPartition && subpart->SubPartition_Of == parent->
                    Mount_Point) {
                    if (!backup_one(subpart))
                        return false;
                }
            }
        }
        time_t stop = time(nullptr);
        int backup_time = static_cast<int>(difftime(stop, start));
        LOGINFO("Partition Backup time: %d\n", backup_time);
        if (part_settings->Part->Backup_Method == BackupMethod::BM_FILES)
            part_settings->file_time += backup_time;
        else
            part_settings->img_time += backup_time;
        return true;
    }();

    if (!ok) {
        Clean_Backup_Folder(part_settings->Backup_Folder);
        TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
        tw_set_default_metadata(backup_log.c_str());
    }
    TWFunc::SetPerformanceMode(false);
    return ok;
}

void TWPartitionManager::Clean_Backup_Folder(std::string Backup_Folder) {
    // Extensions we should delete when cleaning a failed backup.
    static constexpr const char *exts[] = {".win", ".md5", ".sha2", ".info"};

    gui_msg("backup_clean=Backup Failed. Cleaning Backup Folder.");

    std::error_code ec;
    std::filesystem::directory_iterator it(Backup_Folder, ec);
    if (ec) {
        gui_msg(
            Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Backup_Folder)(ec.message().c_str()));
        return;
    }

    for (const std::filesystem::directory_entry &entry: it) {
        std::string ext = entry.path().extension().string();
        for (const char *e: exts) {
            if (ext == e) {
                const std::filesystem::path &p = entry.path();
                if (unlink(p.c_str()) != 0)
                    LOGINFO("Unable to unlink '%s: %s'\n", p.c_str(), strerror(errno));
                break;
            }
        }
    }
}

int TWPartitionManager::Check_Backup_Cancel() {
    return stop_backup;
}

int TWPartitionManager::Cancel_Backup() {
    std::string Backup_Folder, Backup_Name, Full_Backup_Path;

    stop_backup = true;

    if (tar_fork_pid != 0) {
        DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
        DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
        Full_Backup_Path = Backup_Folder + "/" + Backup_Name;
        LOGINFO("Killing pid: %d\n", tar_fork_pid);
        kill(tar_fork_pid, SIGUSR2);
        while (kill(tar_fork_pid, 0) == 0) {
            usleep(1000);
        }
        LOGINFO("Backup_Run stopped and returning false, backup cancelled.\n");
        LOGINFO("Removing directory %s\n", Full_Backup_Path.c_str());
        TWFunc::removeDir(Full_Backup_Path, false);
        tar_fork_pid = 0;
    }

    return 0;
}

bool TWPartitionManager::Run_Backup(bool adbbackup) {
    PartitionSettings part_settings;
    int partition_count = 0, disable_free_space_check = 0, skip_digest = 0;
    std::string Backup_Name, Backup_List, backup_path;
    unsigned long long total_bytes = 0, free_space = 0;
    TWPartition *storage = nullptr;
    struct tm *t;
    time_t seconds, total_start, total_stop;
    size_t start_pos = 0, end_pos = 0;
    bool backup_folder_made = false;
    stop_backup = false;
    seconds = time(0);
    t = localtime(&seconds);

    part_settings.img_bytes_remaining = 0;
    part_settings.file_bytes_remaining = 0;
    part_settings.img_time = 0;
    part_settings.file_time = 0;
    part_settings.img_bytes = 0;
    part_settings.file_bytes = 0;
    part_settings.PM_Method = PartitionManagerOp::PM_BACKUP;

    part_settings.adbbackup = adbbackup;
    time(&total_start);

    Update_System_Details();

    if (!Mount_Current_Storage(true))
        return false;

    DataManager::GetValue(TW_SKIP_DIGEST_GENERATE_VAR, skip_digest);
    if (skip_digest == 0)
        part_settings.generate_digest = true;
    else
        part_settings.generate_digest = false;

    DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, part_settings.Backup_Folder);
    DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
    if (Backup_Name == gui_lookup("curr_date", "(Current Date)")) {
        Backup_Name = TWFunc::Get_Current_Date();
    } else if (Backup_Name == gui_lookup("auto_generate", "(Auto Generate)") ||
               Backup_Name == "0" || Backup_Name.empty()) {
        TWFunc::Auto_Generate_Backup_Name();
        DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
    }

    LOGINFO("Backup Name is: '%s'\n", Backup_Name.c_str());

    part_settings.Backup_Folder = (std::filesystem::path(part_settings.Backup_Folder ) / Backup_Name);

    LOGINFO("Backup_Folder is: '%s'\n", part_settings.Backup_Folder.c_str());

    LOGINFO("Calculating backup details...\n");
    DataManager::GetValue("tw_backup_list", Backup_List);
    LOGINFO("Backup_List: %s\n", Backup_List.c_str());
    if (!Backup_List.empty()) {
        end_pos = Backup_List.find(";", start_pos);
        while (end_pos != std::string::npos && start_pos < Backup_List.size()) {
            backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
            LOGINFO("backup_path: %s\n", backup_path.c_str());
            part_settings.Part = Find_Partition_By_Path(backup_path);
            if (part_settings.Part) {
                partition_count++;
                if (part_settings.Part->Backup_Method == BackupMethod::BM_FILES)
                    part_settings.file_bytes += part_settings.Part->Backup_Size;
                else
                    part_settings.img_bytes += part_settings.Part->Backup_Size;
                if (part_settings.Part->Has_SubPartition) {
                    for (TWPartition *subpart: Partitions) {
                        if (subpart->Can_Be_Backed_Up && subpart->Is_Present && subpart->Is_SubPartition && subpart->
                            SubPartition_Of == part_settings.Part->Mount_Point) {
                            partition_count++;
                            if (subpart->Backup_Method == BackupMethod::BM_FILES)
                                part_settings.file_bytes += subpart->Backup_Size;
                            else
                                part_settings.img_bytes += subpart->Backup_Size;
                        }
                    }
                }
            } else {
                gui_msg(Msg(msg::kError,
                            "unable_to_locate_partition=Unable to locate '{1}' partition for backup calculations.")(
                    backup_path));
            }
            start_pos = end_pos + 1;
            end_pos = Backup_List.find(";", start_pos);
        }
    }

    if (partition_count == 0) {
        gui_msg("no_partition_selected=No partitions selected for backup.");
        return false;
    }
    if (adbbackup) {
        if (!twadbbu::Write_ADB_Stream_Header(partition_count)) {
            return false;
        }
    }
    total_bytes = part_settings.file_bytes + part_settings.img_bytes;
    ProgressTracking progress(total_bytes);
    part_settings.progress = &progress;

    gui_msg(Msg("total_partitions_backup= * Total number of partitions to back up: {1}")(partition_count));
    gui_msg(Msg("total_backup_size= * Total size of all data: {1}MB")(total_bytes / 1024 / 1024));
    storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
    if (storage) {
        free_space = storage->Free;
        gui_msg(Msg("available_space= * Available space: {1}MB")(free_space / 1024 / 1024));
    } else {
        gui_err("unable_locate_storage=Unable to locate storage device.");
        return false;
    }

    DataManager::GetValue(TW_DISABLE_FREE_SPACE_VAR, disable_free_space_check);

    if (adbbackup)
        disable_free_space_check = true;

    if (!disable_free_space_check) {
        if (free_space - (32 * 1024 * 1024) < total_bytes) {
            // We require an extra 32MB just in case
            gui_err("no_space=Not enough free space on storage.");
            return false;
        }
    }
    part_settings.img_bytes_remaining = part_settings.img_bytes;
    part_settings.file_bytes_remaining = part_settings.file_bytes;

    gui_msg("backup_started=[BACKUP STARTED]");

    int is_decrypted = 0;
    int is_encrypted = 0;

    DataManager::GetValue(TW_IS_DECRYPTED, is_decrypted);
    DataManager::GetValue(TW_IS_ENCRYPTED, is_encrypted);
    if (!adbbackup || (!is_encrypted || (is_encrypted && is_decrypted))) {
        gui_msg(Msg("backup_folder= * Backup Folder: {1}")(part_settings.Backup_Folder));
        if (!TWFunc::Recursive_Mkdir(part_settings.Backup_Folder)) {
            gui_err("fail_backup_folder=Failed to make backup folder.");
            return false;
        }
        backup_folder_made = true;
    }

    // Half of a backup restores to half of a system, so do not leave one in
    // the list looking like something that could be restored.
    auto discard_unfinished_backup = [&]() {
        if (backup_folder_made && TWFunc::removeDir(part_settings.Backup_Folder, false) != 0)
            LOGERR("Unable to remove '%s'\n", part_settings.Backup_Folder.c_str());
    };

    DataManager::SetProgress(0.0);

    start_pos = 0;
    end_pos = Backup_List.find(";", start_pos);
    while (end_pos != std::string::npos && start_pos < Backup_List.size()) {
        if (stop_backup) {
            discard_unfinished_backup();
            return false;
        }
        backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
        part_settings.Part = Find_Partition_By_Path(backup_path);
        if (part_settings.Part) {
            if (!Backup_Partition(&part_settings)) {
                discard_unfinished_backup();
                return false;
            }
        } else {
            gui_msg(Msg(msg::kError,
                        "unable_to_locate_partition=Unable to locate '{1}' partition for backup calculations.")(
                backup_path));
        }
        start_pos = end_pos + 1;
        end_pos = Backup_List.find(";", start_pos);
    }

    // Average BPS
    if (part_settings.img_time == 0)
        part_settings.img_time = 1;
    if (part_settings.file_time == 0)
        part_settings.file_time = 1;
    int img_bps = static_cast<int>(part_settings.img_bytes) / static_cast<int>(part_settings.img_time);
    unsigned long long file_bps = part_settings.file_bytes / static_cast<int>(part_settings.file_time);

    if (part_settings.file_bytes != 0)
        gui_msg(Msg("avg_backup_fs=Average backup rate for file systems: {1} MB/sec")(file_bps / (1024 * 1024)));
    if (part_settings.img_bytes != 0)
        gui_msg(Msg("avg_backup_img=Average backup rate for imaged drives: {1} MB/sec")(img_bps / (1024 * 1024)));

    time(&total_stop);
    int total_time = static_cast<int>(difftime(total_stop, total_start));

    uint64_t actual_backup_size;
    if (!adbbackup) {
        TWExclude twe;
        actual_backup_size = twe.Get_Folder_Size(part_settings.Backup_Folder);
    } else
        actual_backup_size = part_settings.file_bytes + part_settings.img_bytes;
    actual_backup_size /= (1024LLU * 1024LLU);

    int prev_img_bps = 0, use_compression = 0;
    unsigned long long prev_file_bps = 0;
    DataManager::GetValue(TW_BACKUP_AVG_IMG_RATE, prev_img_bps);
    img_bps += (prev_img_bps * 4);
    img_bps /= 5;

    DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
    if (use_compression)
        DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, prev_file_bps);
    else
        DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, prev_file_bps);
    file_bps += (prev_file_bps * 4);
    file_bps /= 5;

    DataManager::SetValue(TW_BACKUP_AVG_IMG_RATE, img_bps);
    if (use_compression)
        DataManager::SetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
    else
        DataManager::SetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

    gui_msg(Msg("total_backed_size=[{1} MB TOTAL BACKED UP]")(actual_backup_size));
    Update_System_Details();
    UnMount_Main_Partitions();
    gui_msg(Msg(msg::kHighlight, "backup_completed=[BACKUP COMPLETED IN {1} SECONDS]")(total_time)); // the end
    std::string backup_log = part_settings.Backup_Folder + "/recovery.log";
    TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
    tw_set_default_metadata(backup_log.c_str());

    if (part_settings.adbbackup) {
        if (!twadbbu::Write_ADB_Stream_Trailer()) {
            return false;
        }
    }
    part_settings.adbbackup = false;
    DataManager::SetValue("tw_enable_adb_backup", 0);

    return true;
}

bool TWPartitionManager::Restore_Partition(PartitionSettings *part_settings) {
    time_t Start, Stop;

    if (part_settings->adbbackup) {
        std::string partName = std::format("{}.{}.win", part_settings->Part->Backup_Name,
            part_settings->Part->Current_File_System);
        LOGINFO("setting backup name: %s\n", partName.c_str());
        part_settings->Part->Set_Backup_FileName(partName);
    }

    TWFunc::SetPerformanceMode(true);

    time(&Start);

    if (!part_settings->Part->Restore(part_settings)) {
        TWFunc::SetPerformanceMode(false);
        return false;
    }
    if (part_settings->Part->Has_SubPartition && !part_settings->adbbackup) {
        TWPartition *parentPart = part_settings->Part;

        for (TWPartition *subpart: Partitions) {
            part_settings->Part = subpart;
            if (subpart->Is_SubPartition && subpart->SubPartition_Of == parentPart->Mount_Point) {
                part_settings->Part = subpart;
                part_settings->Part->Set_Backup_FileName(std::format("{}.{}.win",
                    part_settings->Part->Backup_Name, part_settings->Part->Current_File_System));
                if (!subpart->Restore(part_settings)) {
                    TWFunc::SetPerformanceMode(false);
                    return false;
                }
            }
        }
    }
    time(&Stop);
    TWFunc::SetPerformanceMode(false);
    gui_msg(Msg("restore_part_done=[{1} done ({2} seconds)]")(part_settings->Part->Backup_Display_Name)(
        static_cast<int>(difftime(Stop, Start))));

    return true;
}

int TWPartitionManager::Run_Restore(const std::string &Restore_Name) {
    PartitionSettings part_settings;
    int check_digest;

    time_t rStart, rStop;
    time(&rStart);
    std::string Restore_List, restore_path;
    size_t start_pos = 0, end_pos;

    part_settings.Backup_Folder = Restore_Name;
    part_settings.Part = nullptr;
    part_settings.partition_count = 0;
    part_settings.total_restore_size = 0;
    part_settings.adbbackup = false;
    part_settings.PM_Method = PartitionManagerOp::PM_RESTORE;

    gui_msg("restore_started=[RESTORE STARTED]");
    gui_msg(Msg("restore_folder=Restore folder: '{1}'")(Restore_Name));

    if (!Mount_Current_Storage(true))
        return false;

    DataManager::GetValue(TW_SKIP_DIGEST_CHECK_VAR, check_digest);
    if (check_digest > 0) {
        // Check Digest files first before restoring to ensure that all of them match before starting a restore
        TWFunc::GUI_Operation_Text(TW_VERIFY_DIGEST_TEXT, gui_parse_text("{@verifying_digest}"));
        gui_msg("verifying_digest=Verifying Digest");
    } else {
        gui_msg("skip_digest=Skipping Digest check based on user setting.");
    }
    gui_msg("calc_restore=Calculating restore details...");
    DataManager::GetValue("tw_restore_selected", Restore_List);

    if (!Restore_List.empty()) {
        end_pos = Restore_List.find(";", start_pos);
        while (end_pos != std::string::npos && start_pos < Restore_List.size()) {
            restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
            part_settings.Part = Find_Partition_By_Path(restore_path);
            if (part_settings.Part) {
                if (part_settings.Part->Mount_Read_Only) {
                    gui_msg(Msg(msg::kError, "restore_read_only=Cannot restore {1} -- mounted read only.")(
                        part_settings.Part->Backup_Display_Name));
                    return false;
                }

                std::string Full_Filename = part_settings.Backup_Folder + "/" + part_settings.Part->Backup_FileName;

                if (tw_get_default_metadata(Get_Android_Root_Path().c_str()) != 0) {
                    gui_msg(Msg(msg::kWarning,
                                "restore_system_context=Unable to get default context for {1} -- Android may not boot.")(
                        Get_Android_Root_Path()));
                }

                if (check_digest > 0 && !twrpDigestDriver::Check_Digest(Full_Filename))
                    return false;
                part_settings.partition_count++;
                part_settings.total_restore_size += part_settings.Part->Get_Restore_Size(&part_settings);
                if (part_settings.Part->Has_SubPartition) {
                    TWPartition *parentPart = part_settings.Part;

                    for (TWPartition *subpart: Partitions) {
                        part_settings.Part = subpart;
                        if (subpart->Is_SubPartition && subpart->SubPartition_Of == parentPart->Mount_Point) {
                            if (check_digest > 0 && !twrpDigestDriver::Check_Digest(Full_Filename))
                                return false;
                            part_settings.total_restore_size += subpart->Get_Restore_Size(&part_settings);
                        }
                    }
                }
            } else {
                gui_msg(
                    Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(
                        restore_path));
            }
            start_pos = end_pos + 1;
            end_pos = Restore_List.find(";", start_pos);
        }
    }

    if (part_settings.partition_count == 0) {
        gui_err("no_part_restore=No partitions selected for restore.");
        return false;
    }

    gui_msg(Msg("restore_part_count=Restoring {1} partitions...")(part_settings.partition_count));
    gui_msg(Msg("total_restore_size=Total restore size is {1}MB")(part_settings.total_restore_size / kMiB));
    DataManager::SetProgress(0.0);
    ProgressTracking progress(part_settings.total_restore_size);
    part_settings.progress = &progress;

    start_pos = 0;
    if (!Restore_List.empty()) {
        end_pos = Restore_List.find(";", start_pos);
        while (end_pos != std::string::npos && start_pos < Restore_List.size()) {
            restore_path = Restore_List.substr(start_pos, end_pos - start_pos);

            part_settings.Part = Find_Partition_By_Path(restore_path);
            if (part_settings.Part) {
                part_settings.partition_count++;
                if (!Restore_Partition(&part_settings))
                    return false;
            } else {
                gui_msg(
                    Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(
                        restore_path));
            }
            start_pos = end_pos + 1;
            end_pos = Restore_List.find(";", start_pos);
        }
    }
    TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, gui_parse_text("{@updating_system_details}"));
    tw_set_default_metadata(Get_Android_Root_Path().c_str());
    UnMount_By_Path(Get_Android_Root_Path(), false);
    Update_System_Details();
    UnMount_Main_Partitions();
    time(&rStop);
    gui_msg(Msg(msg::kHighlight, "restore_completed=[RESTORE COMPLETED IN {1} SECONDS]")(
        static_cast<int>(difftime(rStop, rStart))));
    TWPartition *Decrypt_Data = Find_Partition_By_Path("/data");
    if (Decrypt_Data && Decrypt_Data->Is_Encrypted)
        gui_msg(Msg(msg::kWarning, "reboot_after_restore=It is recommended to reboot Android once after first boot."));
    DataManager::SetValue("tw_file_progress", "");

    return true;
}

void TWPartitionManager::Set_Restore_Files(std::string Restore_Name) {
    // Start with the default values
    std::string Restore_List;
    bool get_date = true, check_encryption = true;
    bool adbbackup = false;

    DataManager::SetValue("tw_restore_encrypted", 0);
    if (twadbbu::Check_ADB_Backup_File(Restore_Name)) {
        std::vector<std::string> adb_files;
        adb_files = twadbbu::Get_ADB_Backup_Files(Restore_Name);
        for (const std::string &adb_restore_file: adb_files) {
            std::size_t pos = adb_restore_file.find_first_of(".");
            std::string path = "/" + adb_restore_file.substr(0, pos);
            TWPartition *Part = Find_Partition_By_Path(path);
            if (Part == nullptr) {
                gui_msg(Msg(msg::kError,
                            "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(path));
                continue;
            }
            Restore_List = path + ";";
            Part->Backup_FileName = TWFunc::Get_Filename(adb_restore_file);
            adbbackup = true;
        }
        DataManager::SetValue("tw_enable_adb_backup", 1);
    } else {
        std::error_code ec;
        std::filesystem::directory_iterator it(Restore_Name, ec);
        if (ec) {
            gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Restore_Name)(
                ec.message().c_str()));
            return;
        }

        for (const std::filesystem::directory_entry &entry: it) {
            std::string name = entry.path().filename().string();
            if (name.size() <= 2)
                continue;

            if (get_date) {
                struct stat st;
                std::string file_path = Restore_Name + "/" + name;
                stat(file_path.c_str(), &st);
                std::string backup_date = ctime(reinterpret_cast<const time_t *>(&st.st_mtime));
                DataManager::SetValue(TW_RESTORE_FILE_DATE, backup_date);
                get_date = false;
            }

            // Strip off three components: label.fstype.extn (split on the first two '.')
            std::string::size_type first_dot = name.find('.');
            if (first_dot == std::string::npos)
                continue;
            std::string::size_type second_dot = name.find('.', first_dot + 1);
            if (second_dot == std::string::npos)
                continue;
            std::string label = name.substr(0, first_dot);
            std::string fstype = name.substr(first_dot + 1, second_dot - first_dot - 1);
            std::string extn = name.substr(second_dot + 1);

            if (fstype == "log")
                continue;
            int extnlength = extn.size();
            if (extnlength != 3 && extnlength != 6)
                continue;
            if (extnlength >= 3 && extn.compare(0, 3, "win") != 0)
                continue;
            //if (extnlength == 6 && strncmp(extn, "win000", 6) != 0) continue;

            if (check_encryption) {
                std::string filename = Restore_Name + "/" + name;
                if (TWFunc::Get_File_Type(filename) == 2) {
                    LOGINFO("'%s' is encrypted\n", filename.c_str());
                    DataManager::SetValue("tw_restore_encrypted", 1);
                }
            }
            if (extnlength == 6 && extn != "win000")
                continue;

            TWPartition *Part = Find_Partition_By_Path(label);
            if (Part == nullptr) {
                gui_msg(Msg(msg::kError,
                            "unable_locate_part_backup_name=Unable to locate partition by backup name: '{1}'")(label));
                continue;
            }

            Part->Backup_FileName = name;
            if (extn.size() > 3) {
                Part->Backup_FileName.resize(Part->Backup_FileName.size() - extn.size() + 3);
            }

            if (!Part->Is_SubPartition) {
                if (Part->Backup_Path == Get_Android_Root_Path())
                    Restore_List += "/system;";
                else
                    Restore_List += Part->Backup_Path + ";";
            }
        }
    }

    if (adbbackup) {
        Restore_List = "ADB_Backup;";
        adbbackup = false;
    }

    // Set the final value
    DataManager::SetValue("tw_restore_list", Restore_List);
    DataManager::SetValue("tw_restore_selected", Restore_List);
    return;
}

int TWPartitionManager::Wipe_By_Path(std::string Path) {
    int ret = false;
    bool found = false;
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    if (Local_Path == "/system")
        Local_Path = Get_Android_Root_Path();
    if (Path == "/cache") {
        TWPartition *cache = Find_Partition_By_Path("/cache");
        if (cache == nullptr) {
            TWPartition *dat = Find_Partition_By_Path("/data");
            if (dat) {
                dat->Wipe_Data_Cache();
                found = true;
            }
        }
    }
    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path || (
                !partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path)) {
            // iterate through all partitions since some legacy devices uses other partitions as vendor causes issues while wiping
            partition->Find_Actual_Block_Device();
            for (TWPartition *part1: Partitions) {
                part1->Find_Actual_Block_Device();
                if (partition->Actual_Block_Device == part1->Actual_Block_Device && partition->Mount_Point != part1->
                    Mount_Point)
                    part1->UnMount(false);
            }
            if (Path == "/and-sec")
                ret = partition->Wipe_AndSec();
            else
                ret = partition->Wipe();
            found = true;
        } else if (partition->Is_SubPartition && partition->SubPartition_Of == Local_Path) {
            partition->Wipe();
        }
    }
    if (found) {
        return ret;
    } else
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
    return false;
}

int TWPartitionManager::Wipe_By_Path(std::string Path, std::string New_File_System) {
    int ret = false;
    bool found = false;
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path || (
                !partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path)) {
            if (Path == "/and-sec")
                ret = partition->Wipe_AndSec();
            else
                ret = partition->Wipe(New_File_System);
            found = true;
        } else if (partition->Is_SubPartition && partition->SubPartition_Of == Local_Path) {
            partition->Wipe(New_File_System);
        }
    }
    if (found) {
        return ret;
    } else
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
    return false;
}

int TWPartitionManager::Factory_Reset() {
    int ret = true;

    for (TWPartition *partition: Partitions) {
        if (partition->Wipe_During_Factory_Reset && partition->Is_Present) {
            if (!partition->Wipe())
                ret = false;
        } else if (partition->Has_Android_Secure) {
            if (!partition->Wipe_AndSec())
                ret = false;
        }
    }
    TWFunc::check_and_run_script("/system/bin/factoryreset.sh", "Factory Reset Script");
    return ret;
}

int TWPartitionManager::Wipe_Dalvik_Cache() {
    struct stat st;
    std::vector<std::string> dir;

    if (!Mount_By_Path("/data", true))
        return false;

    dir.push_back("/data/dalvik-cache");

    std::string cacheDir = TWFunc::get_log_dir();
    if (cacheDir == CACHE_LOGS_DIR) {
        if (!PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, false)) {
            LOGINFO("Unable to mount %s for wiping cache.\n", CACHE_LOGS_DIR);
        }
        dir.push_back(cacheDir + "dalvik-cache");
        dir.push_back(cacheDir + "/dc");
    }

    TWPartition *sdext = Find_Partition_By_Path("/sd-ext");
    if (sdext && sdext->Is_Present && sdext->Mount(false)) {
        if (stat("/sd-ext/dalvik-cache", &st) == 0) {
            dir.push_back("/sd-ext/dalvik-cache");
        }
    }

    if (cacheDir == CACHE_LOGS_DIR) {
        gui_msg("wiping_cache_dalvik=Wiping Dalvik Cache Directories...");
    } else {
        gui_msg("wiping_dalvik=Wiping Dalvik Directory...");
    }
    for (const auto &d: dir) {
        if (stat(d.c_str(), &st) == 0) {
            TWFunc::removeDir(d, false);
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

int TWPartitionManager::Wipe_Rotate_Data() {
    if (!Mount_By_Path("/data", true))
        return false;

    unlink("/data/misc/akmd*");
    unlink("/data/misc/rild*");
    gui_print("Rotation data wiped.\n");
    return true;
}

int TWPartitionManager::Wipe_Battery_Stats() {
    struct stat st;

    if (!Mount_By_Path("/data", true))
        return false;

    if (0 != stat("/data/system/batterystats.bin", &st)) {
        gui_print("No Battery Stats Found. No Need To Wipe.\n");
    } else {
        remove("/data/system/batterystats.bin");
        gui_print("Cleared battery stats.\n");
    }
    return true;
}

int TWPartitionManager::Wipe_Android_Secure() {
    int ret = false;
    bool found = false;

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Has_Android_Secure) {
            ret = partition->Wipe_AndSec();
            found = true;
        }
    }
    if (found) {
        return ret;
    } else {
        gui_err("no_andsec=No android secure partitions found.");
    }
    return false;
}

int TWPartitionManager::Format_Data() {
    TWPartition *dat = Find_Partition_By_Path("/data");
    TWPartition *metadata = Find_Partition_By_Path("/metadata");
    int ret = false;
    if (metadata)
        metadata->UnMount(false);

    if (dat) {
        if (android::base::GetBoolProperty("ro.virtual_ab.enabled", false)) {
#ifndef TW_EXCLUDE_APEX
            twrpApex apex;
            apex.Unmount();
#endif
            if (metadata)
                metadata->Mount(true);
            if (!Check_Pending_Merges())
                return false;
        }
        ret = dat->Wipe_Encryption();
        if (ret)
            TWFunc::check_and_run_script("/system/bin/formatdata.sh", "Format Data Script");
        return ret;
    } else {
        gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/data"));
        return false;
    }
    return false;
}

int TWPartitionManager::Wipe_Media_From_Data() {
    TWPartition *dat = Find_Partition_By_Path("/data");

    if (dat) {
        if (!dat->Has_Data_Media) {
            LOGERR("This device does not have /data/media\n");
            return false;
        }
        if (!dat->Mount(true))
            return false;

        gui_msg("wiping_datamedia=Wiping internal storage -- /data/media...");
        Remove_MTP_Storage(dat->MTP_Storage_ID);
        TWFunc::removeDir("/data/media", false);
        dat->Recreate_Media_Folder();
        Add_MTP_Storage(dat->MTP_Storage_ID);
        return true;
    } else {
        gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/data"));
        return false;
    }
    return false;
}

int TWPartitionManager::Repair_By_Path(std::string Path, bool Display_Error) {
    int ret = false;
    bool found = false;
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    if (Local_Path == "/tmp" || Local_Path == "/")
        return true;

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path || (
                !partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path)) {
            ret = partition->Repair();
            found = true;
        } else if (partition->Is_SubPartition && partition->SubPartition_Of == Local_Path) {
            partition->Repair();
        }
    }
    if (found) {
        return ret;
    } else if (Display_Error) {
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
    } else {
        LOGINFO("Repair: Unable to find partition for path '%s'\n", Local_Path.c_str());
    }
    return false;
}

int TWPartitionManager::Resize_By_Path(std::string Path, bool Display_Error) {
    int ret = false;
    bool found = false;
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    if (Local_Path == "/tmp" || Local_Path == "/")
        return true;

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_Point == Local_Path || (
                !partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path)) {
            ret = partition->Resize();
            found = true;
        } else if (partition->Is_SubPartition && partition->SubPartition_Of == Local_Path) {
            partition->Resize();
        }
    }
    if (found) {
        return ret;
    } else if (Display_Error) {
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
    } else {
        LOGINFO("Resize: Unable to find partition for path '%s'\n", Local_Path.c_str());
    }
    return false;
}

void TWPartitionManager::Update_System_Details(bool Defer_Data_Size) {
    int data_size = 0;
    TWPartition *Deferred = nullptr;

    gui_msg("update_part_details=Updating partition details...");
    for (TWPartition *partition: Partitions) {
        // Only /data feeds TW_BACKUP_DATA_SIZE, so only /data is worth deferring.
        bool defer = Defer_Data_Size && partition->Has_Data_Media && partition->Mount_Point == "/data";
        partition->Update_Size(true, defer);
        if (defer)
            Deferred = partition;
        if (partition->Can_Be_Mounted) {
            if (partition->Mount_Point == Get_Android_Root_Path()) {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_SYSTEM_SIZE, backup_display_size);
            } else if (partition->Mount_Point == "/data" || partition->Mount_Point == "/datadata") {
                data_size += static_cast<int>(partition->Backup_Size / kMiB);
            } else if (partition->Mount_Point == "/cache") {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_CACHE_SIZE, backup_display_size);
            } else if (partition->Mount_Point == "/sd-ext") {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_SDEXT_SIZE, backup_display_size);
                if (partition->Backup_Size == 0) {
                    DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
                    DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
                } else
                    DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
            } else if (partition->Has_Android_Secure) {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_ANDSEC_SIZE, backup_display_size);
                if (partition->Backup_Size == 0) {
                    DataManager::SetValue(TW_HAS_ANDROID_SECURE, 0);
                    DataManager::SetValue(TW_BACKUP_ANDSEC_VAR, 0);
                } else
                    DataManager::SetValue(TW_HAS_ANDROID_SECURE, 1);
            } else if (partition->Mount_Point == "/boot") {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
                if (partition->Backup_Size == 0) {
                    DataManager::SetValue("tw_has_boot_partition", 0);
                    DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
                } else
                    DataManager::SetValue("tw_has_boot_partition", 1);
            }
        } else {
            // Handle unmountable partitions in case we reset defaults
            if (partition->Mount_Point == "/boot") {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
                if (partition->Backup_Size == 0) {
                    DataManager::SetValue(TW_HAS_BOOT_PARTITION, 0);
                    DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
                } else
                    DataManager::SetValue(TW_HAS_BOOT_PARTITION, 1);
            } else if (partition->Mount_Point == "/recovery") {
                int backup_display_size = static_cast<int>(partition->Backup_Size / kMiB);
                DataManager::SetValue(TW_BACKUP_RECOVERY_SIZE, backup_display_size);
                if (partition->Backup_Size == 0) {
                    DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 0);
                    DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
                } else
                    DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 1);
            } else if (partition->Mount_Point == "/data") {
                data_size += static_cast<int>(partition->Backup_Size / kMiB);
            }
        }
    }
    gui_msg("update_part_details_done=...done");
    DataManager::SetValue(TW_BACKUP_DATA_SIZE, data_size);
    std::string current_storage_path = DataManager::GetCurrentStoragePath();
    TWPartition *FreeStorage = Find_Partition_By_Path(current_storage_path);
    if (FreeStorage) {
        // Attempt to mount storage
        if (!FreeStorage->Mount(false)) {
            gui_msg(Msg(msg::kWarning, "unable_to_mount_storage=Unable to mount storage"));
            DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
        } else {
            DataManager::SetValue(TW_STORAGE_FREE_SIZE, static_cast<int>(FreeStorage->Free / kMiB));
        }
    } else {
        LOGINFO("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
    }
    if (!Write_Fstab())
        LOGERR("Error creating fstab\n");
    // A locked /data holds nothing a backup could restore, so leave the walk
    // until something actually asks for the number.
    if (Deferred && !TWPartition::Data_Is_Locked())
        Deferred->Update_Data_Size_Async();
    return;
}

void TWPartitionManager::Process_Async_Data_Size() {
    TWPartition *dat = Find_Partition_By_Path("/data");
    if (dat)
        dat->Apply_Async_Data_Size();
}

void TWPartitionManager::Post_Decrypt(const std::string &Block_Device) {
    TWPartition *dat = Find_Partition_By_Path("/data");

    if (dat) {
        // reparse for /cache/recovery/command
        static constexpr const char *COMMAND_FILE = "/data/cache/command";
        if (TWFunc::Path_Exists(COMMAND_FILE)) {
            startupArgs startup;
            std::string content;
            TWFunc::read_file(COMMAND_FILE, content);
            std::vector<std::string> args = {content};
            startup.processRecoveryArgs(args, 0);
        }

        DataManager::SetValue(TW_IS_DECRYPTED, 1);
        dat->Is_Decrypted = true;
        if (!Block_Device.empty()) {
            dat->Decrypted_Block_Device = Block_Device;
            gui_msg(Msg("decrypt_success_dev=Data successfully decrypted, new block device: '{1}'")(Block_Device));
        } else {
            gui_msg("decrypt_success_nodev=Data successfully decrypted");
        }
        android::base::SetProperty("twrp.decrypt.done", "true");
        dat->Setup_File_System(false);
        dat->Current_File_System = dat->Fstab_File_System;
        // Needed if we're ignoring blkid because encrypted devices start out as emmc

        // Mount only /data
        dat->Symlink_Path = ""; // Not to let it to bind mount /data/media again
        // The mount is what the wait here was ever for, so wait on that.
        int retry_count = 20;
        while (!dat->Mount(false) && --retry_count)
            usleep(50000);
        if (!dat->Mount(false)) {
            LOGERR("Unable to mount /data after decryption");
        }

        if (dat->Has_Data_Media && TWFunc::Path_Exists("/data/media/0")) {
            dat->Storage_Path = "/data/media/0";
        } else {
            dat->Storage_Path = "/data/media";
        }
        dat->Symlink_Path = dat->Storage_Path;
        DataManager::SetValue("tw_storage_path", dat->Symlink_Path);
        DataManager::SetValue("tw_settings_path", TW_STORAGE_PATH);
        LOGINFO("New storage path after decryption: %s\n", dat->Storage_Path.c_str());

        DataManager::LoadTWRPFolderInfo();
        Update_System_Details(true);
        Output_Partition(dat);
        if (!android::base::StartsWith(dat->Actual_Block_Device, "/dev/block/mmcblk")) {
            if (!dat->Bind_Mount(false))
                LOGERR("Unable to bind mount /sdcard to %s\n", dat->Storage_Path.c_str());
        }
    } else
        LOGERR("Unable to locate data partition.\n");
}

void TWPartitionManager::Parse_Users() {
#ifdef TW_INCLUDE_FBE
    for (int userId = 0; userId <= 9999; userId++) {
        std::string prop = std::format("twrp.user.{}.decrypt", userId);
        std::string user_check_result = android::base::GetProperty(prop, "-1");
        if (user_check_result != "-1") {
            if (userId < 0 || userId > 9999) {
                LOGINFO("Incorrect user id %d\n", userId);
                continue;
            }
            struct users_struct user;
            std::string user_id_str = std::to_string(userId);
            user.userId = user_id_str;

            // Attempt to get name of user. Fallback to user ID if this fails.
            std::string path = std::format("/data/system/users/{}.xml", userId);
            if (!TWFunc::Check_Xml_Format(path)) {
                std::string oldpath = path;
                if (TWFunc::abx_to_xml(oldpath, path)) {
                    LOGINFO("Android 12+: '%s' has been converted into plain text xml (for user %s).\n",
                            oldpath.c_str(), user.userId.c_str());
                }
            }
            std::unique_ptr<char, decltype(&free)> userFile(PageManager::LoadFileToBuffer(path, nullptr), free);
            if (userFile == nullptr) {
                user.userName = user_id_str;
            } else {
                auto userXml = std::make_unique<xml_document<> >();
                userXml->parse < 0 > (userFile.get());
                xml_node<> *userNode = userXml->first_node("user");
                if (userNode == nullptr) {
                    user.userName = user_id_str;
                } else {
                    xml_node<> *nameNode = userNode->first_node("name");
                    if (nameNode == nullptr)
                        user.userName = user_id_str;
                    else {
                        std::string userName = nameNode->value();
                        user.userName = std::format("{} ({})", userName, userId);
                    }
                }
            }

            std::string filename;
            user.type = android::keystore::Get_Password_Type(userId, filename);

            user.isDecrypted = false;
            if (user_check_result == "1")
                user.isDecrypted = true;
            Users_List.push_back(user);
        }
    }
    Check_Users_Decryption_Status();
#endif
}

std::vector<users_struct> *TWPartitionManager::Get_Users_List() {
    return &Users_List;
}

void TWPartitionManager::Mark_User_Decrypted(int userID) {
#ifdef TW_INCLUDE_FBE
    for (users_struct &user: Users_List) {
        if (atoi(user.userId.c_str()) == userID) {
            user.isDecrypted = true;
            android::base::SetProperty(std::format("twrp.user.{}.decrypt", userID), "1");
            break;
        }
    }
    Check_Users_Decryption_Status();
#endif
}

void TWPartitionManager::Mark_Data_Locked() {
#ifdef TW_INCLUDE_FBE
    for (users_struct &user: Users_List) {
        if (!user.isDecrypted) continue;
        // fscrypt_unlock_ce_storage() returns early for a user vold still holds
        // a policy for, so the next decrypt would install nothing.
        fscrypt_lock_ce_storage(atoi(user.userId.c_str()));
        user.isDecrypted = false;
        android::base::SetProperty(std::format("twrp.user.{}.decrypt", user.userId), "1");
    }
    Check_Users_Decryption_Status();
    DataManager::SetValue(TW_IS_DECRYPTED, 0);
    DataManager::SetValue(TW_IS_ENCRYPTED, 1);
    android::base::SetProperty("twrp.decrypt.done", "");
    LOGINFO("Data is locked again, its keys did not survive the unmount.\n");
#endif
}

bool TWPartitionManager::Storage_Name_In_Use(const std::string &Name) {
    return std::ranges::any_of(Partitions, [&](TWPartition *partition) {
        return partition->Storage_Name == Name;
    });
}

void TWPartitionManager::Check_Users_Decryption_Status() {
#ifdef TW_INCLUDE_FBE
    int all_is_decrypted = 1;
    for (users_struct &user: Users_List) {
        if (!user.isDecrypted) {
            LOGINFO("User %s is not decrypted.\n", user.userId.c_str());
            all_is_decrypted = 0;
            break;
        }
    }
    if (all_is_decrypted == 1) {
        LOGINFO("All found users are decrypted.\n");
        DataManager::SetValue("tw_all_users_decrypted", "1");
        android::base::SetProperty("twrp.all.users.decrypted", "true");
    } else
        DataManager::SetValue("tw_all_users_decrypted", "0");
#endif
}

int TWPartitionManager::Decrypt_Device(std::string Password, int user_id) {
#ifdef TW_INCLUDE_CRYPTO
    // Mount any partitions that need to be mounted for decrypt
    for (TWPartition *partition: Partitions) {
        if (partition->Mount_To_Decrypt) {
            partition->Mount(true);
        }
    }
    android::base::SetProperty("twrp.mount_to_decrypt", "1");

    Set_Crypto_State();

    if (DataManager::GetIntValue(TW_IS_FBE)) {
#ifdef TW_INCLUDE_FBE
        if (!Mount_By_Path("/data", true)) // /data has to be mounted for FBE
            return -1;

        bool user_need_decrypt = false;
        for (users_struct &user: Users_List) {
            if (atoi(user.userId.c_str()) == user_id && !user.isDecrypted) {
                user_need_decrypt = true;
            }
        }
        if (!user_need_decrypt) {
            LOGINFO("User %d does not require decryption\n", user_id);
            return 0;
        }

        int retry_count = 10;
        while (!TWFunc::Path_Exists("/data/system/users/gatekeeper.password.key") && --retry_count)
            usleep(2000); // A small sleep is needed after mounting /data to ensure reliable decrypt...maybe because of DE?
        gui_msg(Msg("decrypting_user_fbe=Attempting to decrypt FBE for user {1}...")(user_id));
        if (android::keystore::Decrypt_User(user_id, Password)) {
            gui_msg(Msg("decrypt_user_success_fbe=User {1} Decrypted Successfully")(user_id));
            Mark_User_Decrypted(user_id);
            if (user_id == 0) {
                Post_Decrypt("");
            }

            return 0;
        } else {
            gui_msg(Msg(msg::kError, "decrypt_user_fail_fbe=Failed to decrypt user {1}")(user_id));
        }
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

int TWPartitionManager::Fix_Contexts() {
    for (TWPartition *partition: Partitions) {
        if (partition->Has_Data_Media) {
            if (partition->Mount(true)) {
                if (fixContexts::fixDataMediaContexts(partition->Mount_Point) != 0)
                    return -1;
            }
        }
    }
    UnMount_Main_Partitions();
    gui_msg("done=Done.");
    return 0;
}

TWPartition *TWPartitionManager::Find_Next_Storage(std::string Path, bool Exclude_Data_Media) {
    std::string Search_Path;
    bool after_match = Path.empty();
    if (!after_match) Search_Path = TWFunc::Get_Root_Path(Path);

    for (TWPartition *partition: Partitions) {
        if (!after_match) {
            // Still locating the Path marker. Skip this partition; when it
            // matches, flip the flag and still skip it (storage search starts
            // from the next partition, mirroring the original iter++/break).
            if (partition->Mount_Point == Search_Path) after_match = true;
            continue;
        }
        if (Exclude_Data_Media && partition->Has_Data_Media) continue;
        if (partition->Is_Storage && partition->Is_Present) return partition;
    }

    return nullptr;
}

int TWPartitionManager::Open_Lun_File(std::string Partition_Path, std::string Lun_File) {
    TWPartition *Part = Find_Partition_By_Path(Partition_Path);

    if (!Part) {
        LOGINFO("Unable to locate '%s' for USB storage mode.", Partition_Path.c_str());
        gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Partition_Path));
        return false;
    }
    LOGINFO("USB mount '%s', '%s' > '%s'\n", Partition_Path.c_str(), Part->Actual_Block_Device.c_str(),
            Lun_File.c_str());
    if (!Part->UnMount(true) || !Part->Is_Present)
        return false;

    if (!TWFunc::write_to_file(Lun_File, Part->Actual_Block_Device)) {
        LOGERR("Unable to write to ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
        return false;
    }
    return true;
}

int TWPartitionManager::usb_storage_enable() {
    char lun_file[255];
    bool has_multiple_lun = false;

    std::string Lun_File_str = CUSTOM_LUN_FILE;
    size_t found = Lun_File_str.find("%");
    if (found != std::string::npos) {
        sprintf(lun_file, CUSTOM_LUN_FILE, 1);
        if (TWFunc::Path_Exists(lun_file))
            has_multiple_lun = true;
    }
    mtp_was_enabled = TWFunc::Toggle_MTP(false); // Must disable MTP for USB Storage
    // On error: restore MTP (if it was enabled before we disabled it) and report failure.
    auto mtp_restore_fail = [this] {
        if (mtp_was_enabled)
            if (!Enable_MTP())
                Disable_MTP();
        return false;
    };
    if (!has_multiple_lun) {
        LOGINFO("Device doesn't have multiple lun files, mount current storage\n");
        sprintf(lun_file, CUSTOM_LUN_FILE, 0);
        if (TWFunc::Get_Root_Path(DataManager::GetCurrentStoragePath()) == "/data") {
            TWPartition *Mount = Find_Next_Storage("", true);
            if (Mount) {
                if (!Open_Lun_File(Mount->Mount_Point, lun_file)) {
                    return mtp_restore_fail();
                }
            } else {
                gui_err("unable_locate_storage=Unable to locate storage device.");
                return mtp_restore_fail();
            }
        } else if (!Open_Lun_File(DataManager::GetCurrentStoragePath(), lun_file)) {
            return mtp_restore_fail();
        }
    } else {
        LOGINFO("Device has multiple lun files\n");
        TWPartition *Mount1;
        TWPartition *Mount2;
        sprintf(lun_file, CUSTOM_LUN_FILE, 0);
        Mount1 = Find_Next_Storage("", true);
        if (Mount1) {
            if (!Open_Lun_File(Mount1->Mount_Point, lun_file)) {
                return mtp_restore_fail();
            }
            sprintf(lun_file, CUSTOM_LUN_FILE, 1);
            Mount2 = Find_Next_Storage(Mount1->Mount_Point, true);
            if (Mount2 && Mount2->Mount_Point != Mount1->Mount_Point) {
                Open_Lun_File(Mount2->Mount_Point, lun_file);
                // Mimic single lun code: Mount CurrentStoragePath if it's not /data
            } else if (TWFunc::Get_Root_Path(DataManager::GetCurrentStoragePath()) != "/data") {
                Open_Lun_File(DataManager::GetCurrentStoragePath(), lun_file);
            }
            // Mimic single lun code: Mount CurrentStoragePath if it's not /data
        } else if (TWFunc::Get_Root_Path(DataManager::GetCurrentStoragePath()) != "/data" && !Open_Lun_File(
                       DataManager::GetCurrentStoragePath(), lun_file)) {
            gui_err("unable_locate_storage=Unable to locate storage device.");
            return mtp_restore_fail();
        }
    }
    android::base::SetProperty("sys.storage.ums_enabled", "1");
    android::base::SetProperty("sys.usb.config", "mass_storage,adb");
    return true;
}

int TWPartitionManager::usb_storage_disable() {
    int index, ret = 0;
    char lun_file[255], ch[2] = {0, 0};
    std::string str = ch;

    for (index = 0; index < 2; index++) {
        sprintf(lun_file, CUSTOM_LUN_FILE, index);
        if (!TWFunc::write_to_file(lun_file, str)) {
            break;
            ret = -1;
        }
    }
    Mount_All_Storage();
    Update_System_Details();
    UnMount_Main_Partitions();
    android::base::SetProperty("sys.storage.ums_enabled", "0");
    android::base::SetProperty("sys.usb.config", "adb");
    if (mtp_was_enabled && !Enable_MTP()) Disable_MTP();

    if (ret < 0 && index == 0) {
        LOGERR("Unable to write to ums lunfile '%s'.", lun_file);
        return false;
    }
    return true;
}

void TWPartitionManager::Mount_All_Storage() {
    for (TWPartition *partition: Partitions) {
        if (partition->Is_Storage) partition->Mount(false);
    }
}

void TWPartitionManager::UnMount_Main_Partitions() {
    // Unmounts system and data if data is not data/media
    // Also unmounts boot if boot is mountable
    LOGINFO("Unmounting main partitions...\n");

    TWPartition *Partition = Find_Partition_By_Path("/vendor");

    if (Partition) UnMount_By_Path("/vendor", false);
    Partition = Find_Partition_By_Path(Get_Android_Root_Path());
    if (Partition) UnMount_By_Path(Get_Android_Root_Path(), true);
    Partition = Find_Partition_By_Path("/product");
    if (Partition) UnMount_By_Path("/product", false);
    if (!datamedia)
        UnMount_By_Path("/data", true);

    Partition = Find_Partition_By_Path("/boot");
    if (Partition && Partition->Can_Be_Mounted)
        Partition->UnMount(true);
}

int TWPartitionManager::Partition_SDCard() {
    std::string Storage_Path, Command, Device, fat_str, ext_str, start_loc, end_loc, ext_format, sd_path, tmpdevice;
    int ext, swap, total_size = 0, fat_size;

    gui_msg("start_partition_sd=Partitioning SD Card...");

    // Locate and validate device to partition
    TWPartition *SDCard = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());

    if (!SDCard) {
        gui_err("partition_sd_locate=Unable to locate device to partition.");
        return false;
    }

    if (!SDCard->Removable || SDCard->Has_Data_Media) {
        gui_err("partition_sd_locate=Unable to locate device to partition.");
        return false;
    }

    // Unmount everything
    if (!SDCard->UnMount(true)) return false;
    TWPartition *SDext = Find_Partition_By_Path("/sd-ext");
    if (SDext) {
        if (!SDext->UnMount(true))
            return false;
    }
    char *swappath = getenv("SWAPPATH");
    if (swappath) {
        LOGINFO("Unmounting swap at '%s'\n", swappath);
        umount(swappath);
    }

    // Determine block device
    if (SDCard->Alternate_Block_Device.empty()) {
        SDCard->Find_Actual_Block_Device();
        Device = SDCard->Actual_Block_Device;
        // Just use the root block device
        Device.resize(strlen("/dev/block/mmcblkX"));
    } else {
        Device = SDCard->Alternate_Block_Device;
    }

    // Find the size of the block device:
    total_size = static_cast<int>(TWFunc::IOCTL_Get_Block_Size(Device.c_str()) / kMiB);

    DataManager::GetValue("tw_sdext_size", ext);
    DataManager::GetValue("tw_swap_size", swap);
    DataManager::GetValue("tw_sdpart_file_system", ext_format);
    fat_size = total_size - ext - swap;
    LOGINFO(
        "sd card mount point %s block device is '%s', sdcard size is: %iMB, fat size: %iMB, ext size: %iMB, ext system: '%s', swap size: %iMB\n",
        DataManager::GetCurrentStoragePath().c_str(), Device.c_str(), total_size, fat_size, ext, ext_format.c_str(),
        swap);

    // Determine partition sizes
    if (swap == 0 && ext == 0) {
        fat_str = "-0";
    } else {
        fat_str = std::to_string(fat_size) + "MB";
    }
    if (swap == 0) {
        ext_str = "-0";
    } else {
        ext_str = "+" + std::to_string(ext) + "MB";
    }

    if (ext + swap > total_size) {
        gui_err("ext_swap_size=EXT + Swap size is larger than sdcard size.");
        return false;
    }

    gui_msg("remove_part_table=Removing partition table...");
    Command = "sgdisk --zap-all " + Device;
    LOGINFO("Command is: '%s'\n", Command.c_str());
    if (TWFunc::Exec_Cmd(Command) != 0) {
        gui_err("unable_rm_part=Unable to remove partition table.");
        Update_System_Details();
        return false;
    }
    gui_msg(Msg("create_part=Creating {1} partition...")("FAT32"));
    Command = "sgdisk  --new=0:0:" + fat_str +
              " --change-name=0:\"Microsoft basic data\" --typecode=0:EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 " + Device;
    LOGINFO("Command is: '%s'\n", Command.c_str());
    if (TWFunc::Exec_Cmd(Command) != 0) {
        gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("FAT32"));
        return false;
    }
    if (ext > 0) {
        gui_msg(Msg("create_part=Creating {1} partition...")("EXT"));
        Command = "sgdisk --new=0:0:" + ext_str + " --change-name=0:\"Linux filesystem\" " + Device;
        LOGINFO("Command is: '%s'\n", Command.c_str());
        if (TWFunc::Exec_Cmd(Command) != 0) {
            gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("EXT"));
            Update_System_Details();
            return false;
        }
    }
    if (swap > 0) {
        gui_msg(Msg("create_part=Creating {1} partition...")("swap"));
        Command =
                "sgdisk --new=0:0:-0 --change-name=0:\"Linux swap\" --typecode=0:0657FD6D-A4AB-43C4-84E5-0933C84B4F4F "
                + Device;
        LOGINFO("Command is: '%s'\n", Command.c_str());
        if (TWFunc::Exec_Cmd(Command) != 0) {
            gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("swap"));
            Update_System_Details();
            return false;
        }
    }

    // Convert GPT to MBR
    Command = "sgdisk --gpttombr " + Device;
    if (TWFunc::Exec_Cmd(Command) != 0)
        LOGINFO("Failed to covert partition GPT to MBR\n");

    // Tell the kernel to rescan the partition table
    {
        android::base::unique_fd fd(open(Device.c_str(), O_RDONLY));
        ioctl(fd.get(), BLKRRPART, 0);
    }

    std::string format_device = Device;
    if (Device.substr(0, 17) == "/dev/block/mmcblk")
        format_device += "p";

    // Format new partitions to proper file system
    if (fat_size > 0) {
        Command = "mkfs.fat " + format_device + "1";
        TWFunc::Exec_Cmd(Command);
    }
    if (ext > 0) {
        if (!SDext) {
            Command = "mke2fs -t " + ext_format + " -m 0 " + format_device + "2";
            gui_msg(Msg("format_sdext_as=Formatting sd-ext as {1}...")(ext_format));
            LOGINFO("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
            TWFunc::Exec_Cmd(Command);
        } else {
            SDext->Wipe(ext_format);
        }
    }
    if (swap > 0) {
        Command = std::format("mkswap {}{}", format_device, ext > 0 ? 3 : 2);
        TWFunc::Exec_Cmd(Command);
    }

    // Update_System_Details() puts back whatever it had to mount itself, so
    // mount here to leave the card up the way partitioning always has.
    SDCard->Mount(true);

    Update_System_Details();
    gui_msg("part_complete=Partitioning complete.");
    return true;
}

void TWPartitionManager::Get_Partition_List(std::string ListType, std::vector<PartitionList> *Partition_List) {
    if (ListType == "mount") {
        for (TWPartition *partition: Partitions) {
            // Setup_File_System() calls everything mountable, so ask whether
            // there is a device to mount as well.
            if (partition->Can_Be_Mounted && partition->Is_Present) {
                Partition_List->push_back({
                    .Display_Name = partition->Display_Name,
                    .Mount_Point = partition->Mount_Point,
                    .selected = partition->Is_Mounted(),
                });
            }
        }
    } else if (ListType == "storage") {
        std::string Current_Storage = DataManager::GetCurrentStoragePath();
        for (TWPartition *partition: Partitions) {
            // A uevent entry with nothing plugged into it is a place to put a
            // device, not a place to put files.
            if (partition->Is_Storage && partition->Is_Present) {
                Partition_List->push_back({
                    .Display_Name = std::format("{} ({} MB)", partition->Storage_Name, partition->Free / (1024 * 1024)),
                    .Mount_Point = partition->Storage_Path,
                    .selected = partition->Storage_Path == Current_Storage,
                });
            }
        }
    } else if (ListType == "backup") {
        for (TWPartition *partition: Partitions) {
            if (partition->Can_Be_Backed_Up && !partition->Is_SubPartition && partition->Is_Present) {
                unsigned long long Backup_Size = partition->Backup_Size;
                if (partition->Has_SubPartition) {
                    for (TWPartition *subpart: Partitions) {
                        if (subpart->Is_SubPartition && subpart->Can_Be_Backed_Up && subpart->Is_Present
                            && subpart->SubPartition_Of == partition->Mount_Point) {
                            Backup_Size += subpart->Backup_Size;
                        }
                    }
                }
                std::string size_place;
                if (partition->Backup_Size_Provisional && !TWPartition::Data_Is_Locked()) {
                    // The list is on screen, so someone wants the number now.
                    partition->Update_Data_Size_Async();
                    size_place = gui_lookup("calculating", "calculating");
                } else {
                    size_place = std::format("{} MB", Backup_Size / (1024 * 1024));
                }
                Partition_List->push_back({
                    .Display_Name = std::format("{} ({})", partition->Backup_Display_Name, size_place),
                    .Mount_Point = partition->Backup_Path,
                    .selected = false,
                });
            }
        }
    } else if (ListType == "restore") {
        std::string Restore_List;

        DataManager::GetValue("tw_restore_list", Restore_List);
        if (!Restore_List.empty()) {
            for (const std::string &restore_path: android::base::Tokenize(Restore_List, ";")) {
                if (restore_path == "ADB_Backup") {
                    Partition_List->push_back({
                        .Display_Name = "ADB Backup",
                        .Mount_Point = "ADB Backup",
                        .selected = true,
                    });
                    break;
                }
                TWPartition *restore_part = Find_Partition_By_Path(restore_path);
                if (restore_part == nullptr) {
                    gui_msg(
                        Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(
                            restore_path));
                    continue;
                }
                // Don't allow restore of recovery (causes problems on some devices)
                // Don't add subpartitions to the list of items
                if ((restore_part->Backup_Name == "recovery" && !restore_part->Can_Be_Backed_Up) || restore_part->
                    Is_SubPartition) {
                    continue;
                }
                Partition_List->push_back({
                    .Display_Name = restore_part->Backup_Display_Name,
                    .Mount_Point = restore_part->Backup_Path,
                    .selected = true,
                });
            }
        }
    } else if (ListType == "wipe") {
        // dalvik
        Partition_List->push_back({
            .Display_Name = gui_parse_text("{@dalvik}"),
            .Mount_Point = "DALVIK",
            .selected = false,
        });
        for (TWPartition *partition: Partitions) {
            if (partition->Wipe_Available_in_GUI && !partition->Is_SubPartition && partition->Is_Present) {
                Partition_List->push_back({
                    .Display_Name = partition->Display_Name,
                    .Mount_Point = partition->Mount_Point,
                    .selected = false,
                });
            }
            if (partition->Has_Android_Secure) {
                Partition_List->push_back({
                    .Display_Name = partition->Backup_Display_Name,
                    .Mount_Point = partition->Backup_Path,
                    .selected = false,
                });
            }
            if (partition->Has_Data_Media) {
                Partition_List->push_back({
                    .Display_Name = partition->Storage_Name,
                    .Mount_Point = "INTERNAL",
                    .selected = false,
                });
            }
        }
    } else if (ListType == "flashimg") {
        for (TWPartition *partition: Partitions) {
            if (partition->Can_Flash_Img && partition->Is_Present) {
                Partition_List->push_back({
                    .Display_Name = partition->Backup_Display_Name,
                    .Mount_Point = partition->Backup_Path,
                    .selected = false,
                });
            }
        }
        if (DataManager::GetIntValue("tw_has_repack_tools") != 0 &&
            DataManager::GetIntValue("tw_has_boot_slots") != 0 &&
            DataManager::GetIntValue("tw_include_install_recovery_ramdisk") != 0) {
#ifdef BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT
            std::string dest_partition = "/vendor_boot";
#else
            std::string dest_partition = "/boot";
#endif

            TWPartition *boot = Find_Partition_By_Path(dest_partition);
            if (boot) {
                // Allow flashing kernels and ramdisks
                Partition_List->push_back({
                    .Display_Name = gui_lookup("install_twrp_ramdisk", "Install Recovery Ramdisk"),
                    .Mount_Point = "/repack_ramdisk",
                    .selected = false,
                });
                LOGINFO("Install Recovery Ramdisk: target partition=%s\n", dest_partition.c_str());
            }
        }
    } else {
        LOGERR("Unknown list type '%s' requested for TWPartitionManager::Get_Partition_List\n", ListType.c_str());
    }
}

int TWPartitionManager::Fstab_Processed() {
    return Partitions.size();
}

void TWPartitionManager::Output_Storage_Fstab() {
    std::string cacheDir = TWFunc::get_log_dir();

    if (cacheDir.empty()) {
        LOGINFO("Unable to find cache directory\n");
        return;
    }

    std::string storageFstab = TWFunc::get_log_dir() + "recovery/storage.fstab";
    std::ofstream fp(storageFstab);

    if (!fp.is_open()) {
        gui_msg(Msg(msg::kError, "unable_to_open=Unable to open '{1}'.")(storageFstab));
        return;
    }

    // Iterate through all partitions
    for (TWPartition *partition: Partitions) {
        if (partition->Is_Storage) {
            fp << partition->Storage_Path << ';' << partition->Storage_Name << ";\n";
        }
    }
}

TWPartition *TWPartitionManager::Get_Default_Storage_Partition() {
    TWPartition *res = nullptr;
    for (TWPartition *partition: Partitions) {
        if (!partition->Is_Storage)
            continue;

        if (partition->Is_Settings_Storage)
            return partition;

        if (!res)
            res = partition;
    }
    return res;
}

bool TWPartitionManager::Enable_MTP() {
#ifdef TW_HAS_MTP
    if (mtppid) {
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
        std::string vendor = android::base::GetProperty("usb.vendor", "18D1");
        std::string product = android::base::GetProperty("usb.product.mtpadb", "4EE2");
        TWFunc::write_to_file("/sys/class/android_usb/android0/idVendor", vendor);
        TWFunc::write_to_file("/sys/class/android_usb/android0/idProduct", product);
        android::base::SetProperty("sys.usb.config", "mtp,adb");
    }
    /* To enable MTP debug, use the twrp command line feature:
     * twrp set tw_mtp_debug 1
     */
    auto mtp = std::make_unique<TwrpMtp>(DataManager::GetIntValue("tw_mtp_debug"));
    mtppid = mtp->forkserver(mtppipe);
    if (mtppid) {
        close(mtppipe[0]); // Host closes read side
        mtp_write_fd = mtppipe[1];
        DataManager::SetValue("tw_mtp_enabled", 1);
        Add_All_MTP_Storage();
        return true;
    } else {
        close(mtppipe[0]);
        close(mtppipe[1]);
        gui_err("mtp_fail=Failed to enable MTP");
        return false;
    }
#else
    gui_err("no_mtp=MTP support not included");
#endif
    DataManager::SetValue("tw_mtp_enabled", 0);
    return false;
}

void TWPartitionManager::Add_All_MTP_Storage() {
#ifdef TW_HAS_MTP

    if (!mtppid)
        return; // MTP is not enabled

    for (TWPartition *partition: Partitions) {
        if (partition->Is_Storage && partition->Is_Present && partition->Mount(false))
            Add_Remove_MTP_Storage(partition, MTP_MESSAGE_ADD_STORAGE);
    }
#else
    return;
#endif
}

bool TWPartitionManager::Disable_MTP() {
    android::base::SetProperty("sys.usb.ffs.mtp.ready", "0");
    if (android::base::GetProperty("sys.usb.config", "") != "adb") {
        android::base::SetProperty("sys.usb.config", "none");
        std::string vendor = android::base::GetProperty("usb.vendor", "18D1");
        std::string product = android::base::GetProperty("usb.product.adb", "D001");
        TWFunc::write_to_file("/sys/class/android_usb/android0/idVendor", vendor);
        TWFunc::write_to_file("/sys/class/android_usb/android0/idProduct", product);
        usleep(2000);
    }
#ifdef TW_HAS_MTP
    if (mtppid) {
        LOGINFO("Disabling MTP\n");
        int status;
        kill(mtppid, SIGKILL);
        mtppid = 0;
        // We don't care about the exit value, but this prevents a zombie process
        waitpid(mtppid, &status, 0);
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

TWPartition *TWPartitionManager::Find_Partition_By_MTP_Storage_ID(unsigned int Storage_ID) {
    for (TWPartition *partition: Partitions) {
        if (partition->MTP_Storage_ID == Storage_ID) return partition;
    }
    return nullptr;
}

bool TWPartitionManager::Add_Remove_MTP_Storage(TWPartition *Part, int message_type) {
#ifdef TW_HAS_MTP
    struct mtpmsg mtp_message;

    if (!mtppid)
        return false; // MTP is disabled

    if (mtp_write_fd < 0) {
        LOGINFO("MTP: mtp_write_fd is not set\n");
        return false;
    }

    if (Part) {
        if (Part->MTP_Storage_ID == 0)
            return false;
        if (message_type == MTP_MESSAGE_REMOVE_STORAGE) {
            mtp_message.message_type = MTP_MESSAGE_REMOVE_STORAGE; // Remove
            LOGINFO("sending message to remove %i\n", Part->MTP_Storage_ID);
            mtp_message.storage_id = Part->MTP_Storage_ID;
            if (write(mtp_write_fd, &mtp_message, sizeof(mtp_message)) <= 0) {
                LOGINFO("error sending message to remove storage %i\n", Part->MTP_Storage_ID);
                return false;
            } else {
                LOGINFO("Message sent, remove storage ID: %i\n", Part->MTP_Storage_ID);
                return true;
            }
        } else if (message_type == MTP_MESSAGE_ADD_STORAGE && Part->Is_Mounted()) {
            mtp_message.message_type = MTP_MESSAGE_ADD_STORAGE; // Add
            mtp_message.storage_id = Part->MTP_Storage_ID;
            if (Part->Storage_Path.size() >= sizeof(mtp_message.path)) {
                LOGERR("Storage path '%s' too large for mtpmsg\n", Part->Storage_Path.c_str());
                return false;
            }
            strcpy(mtp_message.path, Part->Storage_Path.c_str());
            if (Part->Storage_Name.size() >= sizeof(mtp_message.display)) {
                LOGERR("Storage name '%s' too large for mtpmsg\n", Part->Storage_Name.c_str());
                return false;
            }
            strcpy(mtp_message.display, Part->Storage_Name.c_str());
            mtp_message.maxFileSize = Part->Get_Max_FileSize();
            LOGINFO("sending message to add %i '%s' '%s'\n", mtp_message.storage_id, mtp_message.path,
                    mtp_message.display);
            if (write(mtp_write_fd, &mtp_message, sizeof(mtp_message)) <= 0) {
                LOGINFO("error sending message to add storage %i\n", Part->MTP_Storage_ID);
                return false;
            } else {
                LOGINFO("Message sent, add storage ID: %i '%s'\n", Part->MTP_Storage_ID, mtp_message.path);
                return true;
            }
        } else {
            LOGERR("Unknown MTP message type: %i\n", message_type);
        }
    } else {
        // This hopefully never happens as the error handling should
        // occur in the calling function.
        LOGINFO("TWPartitionManager::Add_Remove_MTP_Storage NULL partition given\n");
    }
    return true;
#else
    gui_err("no_mtp=MTP support not included");
    DataManager::SetValue("tw_mtp_enabled", 0);
    return false;
#endif
}

bool TWPartitionManager::Add_MTP_Storage(std::string Mount_Point) {
#ifdef TW_HAS_MTP
    TWPartition *Part = PartitionManager.Find_Partition_By_Path(Mount_Point);
    if (Part) {
        return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_ADD_STORAGE);
    } else {
        LOGINFO("TWFunc::Add_MTP_Storage unable to locate partition for '%s'\n", Mount_Point.c_str());
    }
#endif
    return false;
}

bool TWPartitionManager::Add_MTP_Storage(unsigned int Storage_ID) {
#ifdef TW_HAS_MTP
    TWPartition *Part = PartitionManager.Find_Partition_By_MTP_Storage_ID(Storage_ID);
    if (Part) {
        return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_ADD_STORAGE);
    } else {
        LOGINFO("TWFunc::Add_MTP_Storage unable to locate partition for %i\n", Storage_ID);
    }
#endif
    return false;
}

bool TWPartitionManager::Remove_MTP_Storage(std::string Mount_Point) {
#ifdef TW_HAS_MTP
    TWPartition *Part = PartitionManager.Find_Partition_By_Path(Mount_Point);
    if (Part) {
        return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_REMOVE_STORAGE);
    } else {
        LOGINFO("TWFunc::Remove_MTP_Storage unable to locate partition for '%s'\n", Mount_Point.c_str());
    }
#endif
    return false;
}

bool TWPartitionManager::Remove_MTP_Storage(unsigned int Storage_ID) {
#ifdef TW_HAS_MTP
    TWPartition *Part = PartitionManager.Find_Partition_By_MTP_Storage_ID(Storage_ID);
    if (Part) {
        return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_REMOVE_STORAGE);
    } else {
        LOGINFO("TWFunc::Remove_MTP_Storage unable to locate partition for %i\n", Storage_ID);
    }
#endif
    return false;
}

bool TWPartitionManager::Flash_Image(std::string &path, std::string &filename) {
    twrpRepacker repacker;
    int partition_count = 0;
    TWPartition *flash_part = nullptr;
    std::string Flash_List, flash_path, full_filename;
    size_t start_pos = 0, end_pos = 0;

    full_filename = path + "/" + filename;

    gui_msg("image_flash_start=[IMAGE FLASH STARTED]");
    gui_msg(Msg("img_to_flash=Image to flash: '{1}'")(full_filename));

    if (!TWFunc::Path_Exists(full_filename)) {
        if (!Mount_By_Path(full_filename, true)) {
            return false;
        }
        if (!TWFunc::Path_Exists(full_filename)) {
            gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")(full_filename));
            return false;
        }
    }

    DataManager::GetValue("tw_flash_partition", Flash_List);
    Repack_Type repack = REPLACE_NONE;
    if (Flash_List == "/repack_ramdisk;") {
        repack = REPLACE_RAMDISK;
    } else if (Flash_List == "/repack_kernel;") {
        repack = REPLACE_KERNEL;
    }
    if (repack != REPLACE_NONE) {
        Repack_Options_struct Repack_Options;
        Repack_Options.Type = repack;
        Repack_Options.Disable_Verity = false;
        Repack_Options.Disable_Force_Encrypt = false;
        Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
        return repacker.Repack_Image_And_Flash(full_filename, Repack_Options);
    }
    PartitionSettings part_settings;
    part_settings.Backup_Folder = path;
    unsigned long long total_bytes = TWFunc::Get_File_Size(full_filename);
    ProgressTracking progress(total_bytes);
    part_settings.progress = &progress;
    part_settings.adbbackup = false;
    part_settings.PM_Method = PartitionManagerOp::PM_RESTORE;
    gui_msg("calc_restore=Calculating restore details...");
    if (!Flash_List.empty()) {
        end_pos = Flash_List.find(";", start_pos);
        while (end_pos != std::string::npos && start_pos < Flash_List.size()) {
            flash_path = Flash_List.substr(start_pos, end_pos - start_pos);
            flash_part = Find_Partition_By_Path(flash_path);
            if (flash_part) {
                partition_count++;
                if (partition_count > 1) {
                    gui_err("too_many_flash=Too many partitions selected for flashing.");
                    return false;
                }
            } else {
                gui_msg(
                    Msg(msg::kError, "flash_unable_locate=Unable to locate '{1}' partition for flashing.")(flash_path));
                return false;
            }
            start_pos = end_pos + 1;
            end_pos = Flash_List.find(";", start_pos);
        }
    }

    if (partition_count == 0) {
        gui_err("no_part_flash=No partitions selected for flashing.");
        return false;
    }

    DataManager::SetProgress(0.0);
    if (flash_part) {
        flash_part->Backup_FileName = filename;
        if (!flash_part->Flash_Image(&part_settings))
            return false;
    } else {
        gui_err("invalid_flash=Invalid flash partition specified.");
        return false;
    }
    gui_highlight("flash_done=IMAGE FLASH COMPLETED]");
    return true;
}

void TWPartitionManager::Translate_Partition(const char *path, const char *resource_name, const char *default_value) {
    TWPartition *part = PartitionManager.Find_Partition_By_Path(path);
    if (part) {
        if (part->Is_Adopted_Storage) {
            part->Display_Name = std::format("{} - {}", part->Display_Name, gui_lookup("data", "Data"));
            part->Backup_Display_Name = part->Display_Name;
            part->Storage_Name = std::format("{} - {}", part->Storage_Name,
                                             gui_lookup("adopted_storage", "Adopted Storage"));
        } else {
            part->Display_Name = gui_lookup(resource_name, default_value);
            part->Backup_Display_Name = part->Display_Name;
        }
    }
}

void TWPartitionManager::Translate_Partition(const char *path, const char *resource_name, const char *default_value,
                                             const char *storage_resource_name, const char *storage_default_value) {
    TWPartition *part = PartitionManager.Find_Partition_By_Path(path);
    if (part) {
        if (part->Is_Adopted_Storage) {
            part->Backup_Display_Name = std::format("{} - {}", part->Display_Name,
                                                    gui_lookup("data_backup", "Data (excl. storage)"));
            part->Display_Name = std::format("{} - {}", part->Display_Name, gui_lookup("data", "Data"));
            part->Storage_Name = std::format("{} - {}", part->Storage_Name,
                                             gui_lookup("adopted_storage", "Adopted Storage"));
        } else {
            part->Display_Name = gui_lookup(resource_name, default_value);
            part->Backup_Display_Name = part->Display_Name;
            if (part->Is_Storage)
                part->Storage_Name = gui_lookup(storage_resource_name, storage_default_value);
        }
    }
}

void TWPartitionManager::Translate_Partition(const char *path, const char *resource_name, const char *default_value,
                                             const char *storage_resource_name, const char *storage_default_value,
                                             const char *backup_name, const char *backup_default) {
    TWPartition *part = PartitionManager.Find_Partition_By_Path(path);
    if (part) {
        if (part->Is_Adopted_Storage) {
            part->Backup_Display_Name = std::format("{} - {}", part->Display_Name,
                                                    gui_lookup(backup_name, backup_default));
            part->Display_Name = std::format("{} - {}", part->Display_Name, gui_lookup("data", "Data"));
            part->Storage_Name = std::format("{} - {}", part->Storage_Name,
                                             gui_lookup("adopted_storage", "Adopted Storage"));
        } else {
            part->Display_Name = gui_lookup(resource_name, default_value);
            part->Backup_Display_Name = gui_lookup(backup_name, backup_default);
            if (part->Is_Storage)
                part->Storage_Name = gui_lookup(storage_resource_name, storage_default_value);
        }
    }
}

void TWPartitionManager::Translate_Partition_Display_Names() {
    LOGINFO("Translating partition display names\n");
    Translate_Partition("/system", "system", "System");
    Translate_Partition("/system_image", "system_image", "System Image");
    Translate_Partition("/vendor", "vendor", "Vendor");
    Translate_Partition("/vendor_image", "vendor_image", "Vendor Image");
    Translate_Partition("/cache", "cache", "Cache");
    Translate_Partition("/boot", "boot", "Boot");
    Translate_Partition("/recovery", "recovery", "Recovery");
    if (!datamedia) {
        Translate_Partition("/data", "data", "Data", "internal", "Internal Storage");
        Translate_Partition("/sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
        Translate_Partition("/internal_sd", "sdcard", "SDCard", "sdcard", "SDCard");
        Translate_Partition("/internal_sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
        Translate_Partition("/emmc", "sdcard", "SDCard", "sdcard", "SDCard");
    } else {
        Translate_Partition("/data", "data", "Data", "internal", "Internal Storage", "data_backup",
                            "Data (excl. storage)");
    }
    Translate_Partition("/external_sd", "microsd", "Micro SDCard", "microsd", "Micro SDCard", "data_backup",
                        "Data (excl. storage)");
    Translate_Partition("/external_sdcard", "microsd", "Micro SDCard", "microsd", "Micro SDCard", "data_backup",
                        "Data (excl. storage)");
    Translate_Partition("/usb-otg", "usbotg", "USB OTG", "usbotg", "USB OTG");
    Translate_Partition("/sd-ext", "sdext", "SD-EXT");

    // Android secure is a special case
    TWPartition *part = PartitionManager.Find_Partition_By_Path("/and-sec");
    if (part)
        part->Backup_Display_Name = gui_lookup("android_secure", "Android Secure");

    // So is super, its name carries the count Setup_Super_Partition() found
    part = PartitionManager.Find_Partition_By_Path("/super");
    if (part)
        part->Backup_Display_Name = std::format("Super ({0} {1})", Super_Partition_List.size(),
                                                gui_lookup("partitions", "partitions"));

    for (TWPartition *partition: Partitions) {
        if (!partition->Sysfs_Entry.empty()) {
            Translate_Partition(partition->Mount_Point.c_str(), "autostorage", "Storage", "autostorage", "Storage");
        }
    }

    // This updates the text on all of the storage selection buttons in the GUI
    DataManager::SetBackupFolder();
}

void TWPartitionManager::Remove_Partition_By_Path(std::string Path) {
    std::string Local_Path = TWFunc::Get_Root_Path(Path);

    // First-match removal: erase the first partition whose Mount_Point (or
    // Symlink_Mount_Point) equals Local_Path, then stop. No `delete` here —
    // ownership lives elsewhere (contrast Remove_Uevent_Devices /
    // Unmap_Super_Devices, which delete and so stay manual erase loops).
    auto iter = std::ranges::find_if(Partitions, [&](TWPartition *partition) {
        return partition->Mount_Point == Local_Path ||
               (!partition->Symlink_Mount_Point.empty() && partition->Symlink_Mount_Point == Local_Path);
    });
    if (iter != Partitions.end()) {
        LOGINFO("Found and erasing '%s' from partition list\n", Local_Path.c_str());
        Partitions.erase(iter);
    }
}

void TWPartitionManager::Override_Active_Slot(const std::string &Slot) {
    LOGINFO("Overriding slot to '%s'\n", Slot.c_str());
    Active_Slot_Display = Slot;
    DataManager::SetValue("tw_active_slot", Slot);
    PartitionManager.Update_System_Details();
}

void TWPartitionManager::Set_Active_Slot(const std::string &Slot) {
    if (Slot != "A" && Slot != "B") {
        LOGERR("Set_Active_Slot invalid slot '%s'\n", Slot.c_str());
        return;
    }
    if (Active_Slot_Display == Slot)
        return;
    LOGINFO("Setting active slot %s\n", Slot.c_str());
#ifdef AB_OTA_UPDATER
    if (!Active_Slot_Display.empty()) {
        const auto module = BootControlClient::WaitForService();
        if (module == nullptr) {
            LOGERR("Error getting bootctrl module.\n");
        } else {
            int32_t slot_number = 0;
            if (Slot == "B")
                slot_number = 1;
            CommandResult result = module->SetActiveBootSlot(slot_number);
            if (!result.success)
                gui_msg(Msg(msg::kError, "unable_set_boot_slot=Error changing bootloader boot slot to {1}")(Slot));
        }
        DataManager::SetValue("tw_active_slot", Slot);
        // Doing this outside of this if block may result in a seg fault because the DataManager may not be ready yet
    }
#else
    LOGERR("Boot slot feature not present\n");
#endif
    Active_Slot_Display = Slot;
    if (Fstab_Processed())
        Update_System_Details();
}

std::string TWPartitionManager::Get_Active_Slot_Suffix() {
    if (Active_Slot_Display == "A")
        return "_a";
    return "_b";
}

std::string TWPartitionManager::Get_Active_Slot_Display() {
    return Active_Slot_Display;
}

std::string TWPartitionManager::Get_Android_Root_Path() {
    return "/system_root";
}

void TWPartitionManager::Remove_Uevent_Devices(const std::string &Mount_Point) {
    std::vector<TWPartition *>::iterator iter;

    for (iter = Partitions.begin(); iter != Partitions.end();) {
        if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Mount_Point) {
            TWPartition *part = *iter;
            LOGINFO("%s was removed by uevent data\n", (*iter)->Mount_Point.c_str());
            (*iter)->UnMount(false);
            rmdir((*iter)->Mount_Point.c_str());
            iter = Partitions.erase(iter);
            delete part;
        } else {
            iter++;
        }
    }
}

void TWPartitionManager::Handle_Uevent(const Uevent_Block_Data &uevent_data) {
    for (TWPartition *partition: Partitions) {
        if (!partition->Sysfs_Entry.empty()) {
            std::string device;
            size_t wildcard = partition->Sysfs_Entry.find("*");
            if (wildcard != std::string::npos) {
                device = partition->Sysfs_Entry.substr(0, wildcard);
            } else {
                device = partition->Sysfs_Entry;
            }
            if (device == uevent_data.sysfs_path.substr(0, device.size())) {
                // Found a match
                if (uevent_data.action == "add") {
                    partition->Primary_Block_Device = std::format("/dev/block/{}", uevent_data.block_device);
                    partition->Alternate_Block_Device = partition->Primary_Block_Device;
                    partition->Is_Present = true;
                    LOGINFO("Found a match '%s' '%s'\n", uevent_data.block_device.c_str(), device.c_str());
                    partition->Find_Actual_Block_Device();
                    return;
                }
                if (uevent_data.action == "remove") {
                    partition->Is_Present = false;
                    partition->Primary_Block_Device = "";
                    partition->Actual_Block_Device = "";
                    Remove_Uevent_Devices(partition->Mount_Point);
                    return;
                }
            }
        }
    }

    if (!PartitionManager.Get_Super_Status())
        LOGINFO("Found no matching fstab entry for uevent device '%s' - %s\n", uevent_data.sysfs_path.c_str(),
            uevent_data.action.c_str());
}

void TWPartitionManager::setup_uevent() {
    if (uevent_pfd.fd >= 0) {
        LOGINFO("uevent already set up\n");
        return;
    }

    // Open hotplug event netlink socket
    struct sockaddr_nl nls = {
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
    if (::bind(uevent_pfd.fd, (struct sockaddr *) &nls, sizeof(struct sockaddr_nl)) < 0) {
        LOGERR("Bind failed\n");
        return;
    }
    set_select_fd();
    Coldboot();
}

Uevent_Block_Data TWPartitionManager::get_event_block_values(char *buf, int len) {
    Uevent_Block_Data ret;
    ret.subsystem = "";
    char *ptr = buf;
    const char *end = buf + len;

    buf[len - 1] = '\0';
    while (ptr < end) {
        // The buffer is a netlink uevent: NUL-separated "KEY=VALUE" records, not one string.
        // `field` is the current record (string_view stops at its terminating NUL, exactly
        // bounding what strncmp over the same record would compare).
        std::string_view field(ptr);
        if (field.starts_with("ACTION=")) {
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
        } else if (field.starts_with("MAJOR=")) {
            ptr += strlen("MAJOR=");
            ret.major = atoi(ptr);
        } else if (field.starts_with("MINOR=")) {
            ptr += strlen("MINOR=");
            ret.minor = atoi(ptr);
        }
        ptr += strlen(ptr) + 1;
    }
    return ret;
}

void TWPartitionManager::read_uevent() {
    char buf[1024];

    int len = recv(uevent_pfd.fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (len == -1) {
        LOGINFO("recv error on uevent\n");
        return;
    }
    Uevent_Block_Data uevent_data = get_event_block_values(buf, len);
    if (uevent_data.subsystem == "block" && uevent_data.type == "disk") {
        PartitionManager.Handle_Uevent(uevent_data);
    }
}

void TWPartitionManager::close_uevent() {
    if (uevent_pfd.fd > 0)
        close(uevent_pfd.fd);
    uevent_pfd.fd = -1;
}

void TWPartitionManager::Add_Partition(TWPartition *Part) {
    Partitions.push_back(Part);
}

void TWPartitionManager::Coldboot_Scan(std::span<const std::string> sysfs_entries,
                                       const std::filesystem::path &Path, int depth) {
    // 仅当路径解析成功时触发(忠实原版:匹配只在 realpath() 成功分支内运行)。
    std::error_code ec;
    if (std::filesystem::path resolved = std::filesystem::canonical(Path, ec); !ec) {
        auto write_path = (resolved / "uevent").string();
        const auto real = resolved.string();
        if (TWFunc::Path_Exists(write_path) &&
            std::ranges::any_of(sysfs_entries, [&](const std::string &e) {
                return real.find(e) != std::string::npos;
            })) {
            TWFunc::write_to_file(write_path, "add\n");
        }
    }

    // 下钻。directory_iterator 在顶层会跟随符号链接(同 opendir);
    // symlink_status() 报告条目自身类型,等价于 DT_DIR 的 d_type 语义。
    std::error_code dec;
    for (const auto &entry: std::filesystem::directory_iterator(Path, dec)) {
        auto name = entry.path().filename().string();
        if (name.empty() || name.starts_with(".")) continue;
        bool is_dir = std::filesystem::symlink_status(entry.path(), dec).type() ==
                      std::filesystem::file_type::directory;
        if (!is_dir && depth > 0) continue;
        if (name.starts_with("ram") || name.starts_with("loop")) // C++20 starts_with
            continue;
        Coldboot_Scan(sysfs_entries, entry.path(), depth + 1);
    }
}

void TWPartitionManager::Coldboot() {
    std::vector<std::string> sysfs_entries;
    for (TWPartition *partition: Partitions)
        if (!partition->Sysfs_Entry.empty())
            sysfs_entries.push_back(partition->Sysfs_Entry.substr(0, partition->Sysfs_Entry.find("*")));

    if (!sysfs_entries.empty())
        Coldboot_Scan(sysfs_entries, "/sys/block", 0);
}

bool TWPartitionManager::Prepare_Empty_Folder(const std::string &Folder) {
    if (TWFunc::Path_Exists(Folder))
        TWFunc::removeDir(Folder, false);
    return TWFunc::Recursive_Mkdir(Folder);
}

std::string TWPartitionManager::Get_Bare_Partition_Name(std::string Mount_Point) {
    if (Mount_Point == "/system_root")
        return "system";
    else
        return TWFunc::Remove_Beginning_Slash(Mount_Point);
}

bool TWPartitionManager::Prepare_Super_Volume(TWPartition *twrpPart) {
    Fstab fstab;
    std::string bare_partition_name = Get_Bare_Partition_Name(twrpPart->Get_Mount_Point());

    Super_Partition_List.push_back(bare_partition_name);
    LOGINFO("Trying to prepare %s from super partition\n", bare_partition_name.c_str());

    std::string blk_device_partition;
#ifdef AB_OTA_UPDATER
    blk_device_partition = bare_partition_name + PartitionManager.Get_Active_Slot_Suffix();
#else
    blk_device_partition = bare_partition_name;
#endif

    FstabEntry fstabEntry = {
        .blk_device = blk_device_partition,
        .mount_point = twrpPart->Get_Mount_Point(),
        .fs_type = twrpPart->Current_File_System,
        .fs_mgr_flags.logical = twrpPart->Is_Super,
    };

    fstab.emplace_back(fstabEntry);
    if (!fs_mgr_update_logical_partition(&fstabEntry)) {
        LOGINFO("unable to update logical partition: %s\n", twrpPart->Get_Mount_Point().c_str());
        return false;
    }

    while (access(fstabEntry.blk_device.c_str(), F_OK) != 0) {
        usleep(100);
    }

    twrpPart->Set_Block_Device(fstabEntry.blk_device);
    twrpPart->Update_Size(true);
    twrpPart->Set_Can_Be_Backed_Up(false);
    twrpPart->Set_Can_Be_Wiped(false);
    std::string bare_partition = std::format("/dev/block/bootdevice/by-name/{}", bare_partition_name);
    if (access(bare_partition.c_str(), F_OK) == -1) {
        LOGINFO("Symlinking %s => %s \n", fstabEntry.blk_device.c_str(), bare_partition.c_str());
        symlink(fstabEntry.blk_device.c_str(), bare_partition.c_str());
        android::base::SetProperty("twrp.super.symlinks_created", "true");
    }

    return true;
}

bool TWPartitionManager::Prepare_All_Super_Volumes() {
    bool status = true;

    // 快照 super 分区指针:遍历独立容器,杜绝边遍历边 erase 的迭代器失效。
    std::vector<TWPartition *> supers;
    for (TWPartition *p: Partitions)
        if (p->Is_Super) supers.push_back(p);

    for (TWPartition *part: supers) {
        if (Prepare_Super_Volume(part)) {
            PartitionManager.Output_Partition(part); // 仅成功才输出(修复 (B) 误输出前一个分区)
        } else {
            status = false;
            std::erase(Partitions, part); // 按指针值移除;vector 不释放对象,与原版一致
        }
    }

    Update_System_Details();
    return status;
}

std::string TWPartitionManager::Get_Super_Partition() {
    const auto module = BootControlClient::WaitForService();
    int32_t slot = module->GetCurrentSlot();
    std::string super_device = fs_mgr_get_super_partition_name(slot);
    return std::format("/dev/block/by-name/{}", super_device);
}

void TWPartitionManager::Setup_Super_Devices() {
    std::string superPart = Get_Super_Partition();
    android::fs_mgr::CreateLogicalPartitions(superPart);
}

void TWPartitionManager::Setup_Super_Partition() {
    TWPartition *superPartition = new TWPartition();
    std::string superPart = Get_Super_Partition();

    superPartition->Backup_Path = "/super";
    superPartition->Mount_Point = "/super";
    superPartition->Actual_Block_Device = superPart;
    superPartition->Alternate_Block_Device = superPart;
    // Translated in Translate_Partition_Display_Names(), no resources yet.
    superPartition->Backup_Display_Name = std::format("Super ({}) partitions", Super_Partition_List.size());
    superPartition->Can_Flash_Img = true;
    superPartition->Current_File_System = "emmc";
    superPartition->Can_Be_Backed_Up = true;
    superPartition->Is_Present = true;
    superPartition->Is_SubPartition = false;
    superPartition->Setup_Image();
    Add_Partition(superPartition);
    PartitionManager.Output_Partition(superPartition);
}

bool TWPartitionManager::Get_Super_Status() {
    return access(Get_Super_Partition().c_str(), F_OK) == 0;
}

bool TWPartitionManager::Recreate_Logs_Dir() {
#ifdef TW_INCLUDE_FBE
    struct passwd pd{};
    struct passwd *pwd_result = nullptr;
    char pwd_buf[512];
    if (getpwnam_r("system", &pd, pwd_buf, sizeof(pwd_buf), &pwd_result) != 0 || pwd_result == nullptr) {
        LOGERR("unable to get system user id\n");
        return false;
    }

    struct group grp{};
    struct group *grp_result = nullptr;
    char grp_buf[512];
    if (getgrnam_r("cache", &grp, grp_buf, sizeof(grp_buf), &grp_result) != 0 || grp_result == nullptr) {
        LOGERR("unable to get cache group id\n");
        return false;
    }

    const std::string abLogsRecoveryDir = (std::filesystem::path(DATA_LOGS_DIR) / "recovery").string();
    if (!TWFunc::Create_Dir_Recursive(abLogsRecoveryDir, S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP, pd.pw_uid,
                                      grp.gr_gid)) {
        LOGERR("Unable to recreate %s\n", abLogsRecoveryDir.c_str());
        return false;
    }
    if (setfilecon(abLogsRecoveryDir.c_str(), "u:object_r:cache_file:s0") != 0) {
        LOGERR("Unable to set contexts for %s\n", abLogsRecoveryDir.c_str());
        return false;
    }
#endif
    return true;
}

void TWPartitionManager::Unlock_Block_Partitions() {
    int OFF = 0;
    std::error_code ec;
    for (const auto &entry: std::filesystem::directory_iterator("/dev/block", ec)) {
        if (std::filesystem::symlink_status(entry.path(), ec).type() != std::filesystem::file_type::block)
            continue;
        const std::string block_device = entry.path().string();
        android::base::unique_fd fd(open(block_device.c_str(), O_RDONLY | O_CLOEXEC));
        if (fd.get() < 0) {
            LOGERR("unable to open block device %s: %s\n", block_device.c_str(), strerror(errno));
            continue;
        }
        if (ioctl(fd.get(), BLKROSET, &OFF) == -1) {
            LOGERR("Unable to unlock %s: %s\n", block_device.c_str(), strerror(errno));
            continue;
        }
    }
}

bool TWPartitionManager::Unmap_Super_Devices() {
    bool destroyed = false;
    auto destroy_if_mapped = [](const std::string &name) {
        const std::string mapper_path = std::format("/dev/block/mapper/{}", name);
        struct stat st;
        if (lstat(mapper_path.c_str(), &st) != 0) {
            if (errno == ENOENT) {
                LOGINFO("dynamic partition %s is already unmapped\n", name.c_str());
                return true;
            }
            LOGERR("Unable to inspect dynamic partition %s: %s\n", name.c_str(), strerror(errno));
            return false;
        }
        if (DestroyLogicalPartition(name))
            return true;
        // It may have disappeared between lstat() and destruction.
        if (lstat(mapper_path.c_str(), &st) != 0 && errno == ENOENT)
            return true;
        return false;
    };
#ifndef TW_EXCLUDE_APEX
    twrpApex apex;
    apex.Unmount();
#endif
    for (auto iter = Partitions.begin(); iter != Partitions.end();) {
        LOGINFO("Checking partition: %s\n", (*iter)->Get_Mount_Point().c_str());
        if ((*iter)->Is_Super) {
            TWPartition *part = *iter;
            std::string bare_partition_name = Get_Bare_Partition_Name((*iter)->Get_Mount_Point());
            std::string blk_device_partition = bare_partition_name;
            if (DataManager::GetStrValue("tw_has_boot_slots") == "1")
                blk_device_partition.append(PartitionManager.Get_Active_Slot_Suffix());
            (*iter)->UnMount(false);
            LOGINFO("removing dynamic partition: %s\n", blk_device_partition.c_str());
            destroyed = destroy_if_mapped(blk_device_partition);
            std::string cow_partition = std::format("{}-cow", blk_device_partition);
            std::string cow_partition_path = std::format("/dev/block/mapper/{}", cow_partition);
            struct stat st;
            if (lstat(cow_partition_path.c_str(), &st) == 0) {
                LOGINFO("removing cow partition: %s\n", cow_partition.c_str());
                destroyed = destroy_if_mapped(cow_partition);
            }
            iter = Partitions.erase(iter);
            delete part;
            if (!destroyed) {
                return false;
            }
        } else {
            ++iter;
        }
    }

    std::error_code ec;
    for (const auto &entry: std::filesystem::directory_iterator("/dev/block/mapper", ec)) {
        if (std::filesystem::symlink_status(entry.path(), ec).type() != std::filesystem::file_type::symlink)
            continue;
        std::string partition = entry.path().filename().string();
        if (partition == "userdata")
            continue;
        LOGINFO("removing dynamic partition: %s\n", partition.c_str());
        destroyed = destroy_if_mapped(partition);
        if (!destroyed)
            return false;
    }
    return true;
}


bool TWPartitionManager::Check_Pending_Merges() {
    auto sm = android::snapshot::SnapshotManager::NewForFirstStageMount();
    if (!sm) {
        LOGERR("Unable to call snapshot manager\n");
        return false;
    }

    if (!Unmap_Super_Devices()) {
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
