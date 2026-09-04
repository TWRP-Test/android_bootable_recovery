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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <mntent.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>

#include <algorithm>
#include <array>
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
#include "twrp-functions.hpp"
#include "twrpTar.hpp"
#include "exclude.hpp"
#include "infomanager.hpp"
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "twrpadbbu/libtwrpadbbu.hpp"

#ifdef TW_INCLUDE_CRYPTO
#include "cryptfs.h"
#include "Decrypt.h"
#endif

extern "C" {
#ifdef USE_EXT4
#include <ext4_utils/make_ext4fs.h>
#endif
#ifdef TW_INCLUDE_CRYPTO
#include "gpt/gpt.h"
#endif
}

#ifdef HAVE_CAPABILITIES
#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#endif

// v2 fstab allows you to specify a mount point of "auto" with no /. These items are given a mount point of /auto* where * == auto_index
static int auto_index = 0;

extern selabel_handle *selinux_handle;
extern bool datamedia;

// 1 mebibyte; the bytes<->MiB unit conversion used throughout (e.g. static_cast<int>(bytes / kMiB)).
constexpr unsigned long long kMiB = 1024ULL * 1024;

struct flag_list {
    const char *name;
    unsigned flag;
};

// Copy from system/core/init/builtins.cpp
const flag_list mount_flags[] = {
    { "noatime",    MS_NOATIME },
    { "noexec",     MS_NOEXEC },
    { "nosuid",     MS_NOSUID },
    { "nodev",      MS_NODEV },
    { "nodiratime", MS_NODIRATIME },
    { "ro",         MS_RDONLY },
    { "rw",         0 },
    { "remount",    MS_REMOUNT },
    { "bind",       MS_BIND },
    { "rec",        MS_REC },
    { "unbindable", MS_UNBINDABLE },
    { "private",    MS_PRIVATE },
    { "slave",      MS_SLAVE },
    { "shared",     MS_SHARED },
    { "nosymfollow", MS_NOSYMFOLLOW },
    { "defaults",   0 },
    { nullptr,      0 },
};

const std::string ignored_mount_items[] = {
    "defaults=",
    "errors=",
    "latemount",
    "sysfs_path=",
};

enum TW_FSTAB_FLAGS {
    TWFLAG_DEFAULTS, // Retain position
    TWFLAG_ANDSEC,
    TWFLAG_BACKUP,
    TWFLAG_BACKUPNAME,
    TWFLAG_BLOCKSIZE,
    TWFLAG_CANBEWIPED,
    TWFLAG_CANENCRYPTBACKUP,
    TWFLAG_DISPLAY,
    TWFLAG_ENCRYPTABLE,
    TWFLAG_FILEENCRYPTION,
    TWFLAG_METADATA_ENCRYPTION,
    TWFLAG_FLASHIMG,
    TWFLAG_FORCEENCRYPT,
    TWFLAG_FSFLAGS,
    TWFLAG_IGNOREBLKID,
    TWFLAG_LENGTH,
    TWFLAG_MOUNTTODECRYPT,
    TWFLAG_QUOTA,
    TWFLAG_REMOVABLE,
    TWFLAG_SETTINGSSTORAGE,
    TWFLAG_STORAGE,
    TWFLAG_STORAGENAME,
    TWFLAG_SUBPARTITIONOF,
    TWFLAG_SYMLINK,
    TWFLAG_USERDATAENCRYPTBACKUP,
    TWFLAG_USERMRF,
    TWFLAG_WIPEDURINGFACTORYRESET,
    TWFLAG_WIPEINGUI,
    TWFLAG_SLOTSELECT,
    TWFLAG_WAIT,
    TWFLAG_VERIFY,
    TWFLAG_CHECK,
    TWFLAG_ALTDEVICE,
    TWFLAG_NOTRIM,
    TWFLAG_VOLDMANAGED,
    TWFLAG_FORMATTABLE,
    TWFLAG_RESIZE,
    TWFLAG_KEYDIRECTORY,
    TWFLAG_WRAPPEDKEY,
    TWFLAG_ADOPTED_MOUNT_DELAY,
    TWFLAG_DM_USE_ORIGINAL_PATH,
    TWFLAG_FS_COMPRESS,
    TWFLAG_LOGICAL,
    TWFLAG_METADATA_CSUM,
};

/* Flags without a trailing '=' are considered dual format flags and can be
 * written as either 'flagname' or 'flagname=', where the character following
 * the '=' is Y,y,1 for true and false otherwise.
 */
const flag_list tw_flags[] = {
    { "andsec",                 TWFLAG_ANDSEC },
    { "backup",                 TWFLAG_BACKUP },
    { "backupname=",            TWFLAG_BACKUPNAME },
    { "blocksize=",             TWFLAG_BLOCKSIZE },
    { "canbewiped",             TWFLAG_CANBEWIPED },
    { "canencryptbackup",       TWFLAG_CANENCRYPTBACKUP },
    { "defaults",               TWFLAG_DEFAULTS },
    { "display=",               TWFLAG_DISPLAY },
    { "encryptable=",           TWFLAG_ENCRYPTABLE },
    { "fileencryption=",        TWFLAG_FILEENCRYPTION },
    { "metadata_encryption=",   TWFLAG_METADATA_ENCRYPTION },
    { "flashimg",               TWFLAG_FLASHIMG },
    { "forceencrypt=",          TWFLAG_FORCEENCRYPT },
    { "fsflags=",               TWFLAG_FSFLAGS },
    { "ignoreblkid",            TWFLAG_IGNOREBLKID },
    { "length=",                TWFLAG_LENGTH },
    { "mounttodecrypt",         TWFLAG_MOUNTTODECRYPT },
    { "quota",                  TWFLAG_QUOTA },
    { "removable",              TWFLAG_REMOVABLE },
    { "settingsstorage",        TWFLAG_SETTINGSSTORAGE },
    { "storage",                TWFLAG_STORAGE },
    { "storagename=",           TWFLAG_STORAGENAME },
    { "subpartitionof=",        TWFLAG_SUBPARTITIONOF },
    { "symlink=",               TWFLAG_SYMLINK },
    { "userdataencryptbackup",  TWFLAG_USERDATAENCRYPTBACKUP },
    { "usermrf",                TWFLAG_USERMRF },
    { "wipeduringfactoryreset", TWFLAG_WIPEDURINGFACTORYRESET },
    { "wipeingui",              TWFLAG_WIPEINGUI },
    { "slotselect",             TWFLAG_SLOTSELECT },
    { "wait",                   TWFLAG_WAIT },
    { "verify",                 TWFLAG_VERIFY },
    { "check",                  TWFLAG_CHECK },
    { "altdevice",              TWFLAG_ALTDEVICE },
    { "notrim",                 TWFLAG_NOTRIM },
    { "voldmanaged=",           TWFLAG_VOLDMANAGED },
    { "formattable",            TWFLAG_FORMATTABLE },
    { "resize",                 TWFLAG_RESIZE },
    { "keydirectory=",          TWFLAG_KEYDIRECTORY },
    { "wrappedkey",             TWFLAG_WRAPPEDKEY },
    { "adopted_mount_delay=",   TWFLAG_ADOPTED_MOUNT_DELAY },
    { "dm_use_original_path",   TWFLAG_DM_USE_ORIGINAL_PATH },
    { "fscompress",             TWFLAG_FS_COMPRESS },
    { "logical",                TWFLAG_LOGICAL },
    { "metadata_csum",          TWFLAG_METADATA_CSUM },
    { nullptr,                  0 },
};

TWPartition::TWPartition() {
    Can_Be_Mounted = false;
    Can_Be_Wiped = false;
    Can_Be_Backed_Up = false;
    Use_Rm_Rf = false;
    Wipe_During_Factory_Reset = false;
    Wipe_Available_in_GUI = false;
    Is_SubPartition = false;
    Has_SubPartition = false;
    SubPartition_Of = "";
    Symlink_Path = "";
    Symlink_Mount_Point = "";
    Mount_Point = "";
    Backup_Path = "";
    Wildcard_Block_Device = false;
    Sysfs_Entry = "";
    Actual_Block_Device = "";
    Primary_Block_Device = "";
    Alternate_Block_Device = "";
    Removable = false;
    Is_Present = false;
    Length = 0;
    Size = 0;
    Used = 0;
    Free = 0;
    Backup_Size = 0;
    Backup_Size_Provisional = false;
    Can_Be_Encrypted = false;
    Is_Encrypted = false;
    Is_Decrypted = false;
    Is_FBE = false;
    Mount_To_Decrypt = false;
    Decrypted_Block_Device = "";
    Display_Name = "";
    Backup_Display_Name = "";
    Storage_Name = "";
    Backup_Name = "";
    Backup_FileName = "";
    Backup_Method = BackupMethod::BM_NONE;
    Can_Encrypt_Backup = false;
    Use_Userdata_Encryption = false;
    Has_Data_Media = false;
    Has_Android_Secure = false;
    Is_Storage = false;
    Is_Settings_Storage = false;
    Storage_Path = "";
    Current_File_System = "";
    Fstab_File_System = "";
    Mount_Flags = 0;
    Mount_Options = "";
    Format_Block_Size = 0;
    Ignore_Blkid = false;
    Crypto_Key_Location = "";
    MTP_Storage_ID = 0;
    Can_Flash_Img = false;
    Mount_Read_Only = false;
    Is_Adopted_Storage = false;
    Adopted_GUID = "";
    SlotSelect = false;
    Key_Directory = "";
    Is_Super = false;
    Adopted_Mount_Delay = 0;
    Original_Path = "";
    Use_Original_Path = false;
    Needs_Fs_Compress = false;
    Needs_Metadata_Csum = false;
}

TWPartition::~TWPartition() {
    // Do nothing
}

static std::vector<std::string> Split_Fstab_Line(std::string_view line) {
    // Splits an fstab line into whitespace-separated tokens, honoring
    // double-quoted regions. A '"' toggles "inside-quote" state and is kept in
    // the token; bytes <= ' ' outside quotes separate tokens (consecutive
    // separators are skipped, matching the legacy null-byte-injection loop).
    // Input is bounded to MAX_FSTAB_LINE_LENGTH-1, the legacy strlcpy bound.
    if (line.size() > MAX_FSTAB_LINE_LENGTH - 1)
        line = line.substr(0, MAX_FSTAB_LINE_LENGTH - 1);

    std::vector<std::string> tokens;
    std::string cur;
    bool in_quote = false;
    for (char c: line) {
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

static std::vector<std::string_view> Split_Quoted(std::string_view s, char sep) {
    // Splits a flags string on `sep`, honoring double-quoted regions: a `sep`
    // inside "..." is not a separator, and the '"' is kept in the token (to be
    // stripped later by the caller, matching the legacy separator->'\n' mark +
    // strtok_r path in Process_TW_Flags). Consecutive/leading/trailing
    // separators are coalesced (strtok_r semantics). Mirrors Split_Fstab_Line's
    // quote handling for a single separator char instead of whitespace.
    // Each token is a contiguous run of `s` (only separator chars are skipped,
    // never reordered), so tokens are returned as zero-copy string_views.
    std::vector<std::string_view> tokens;
    bool in_quote = false;
    size_t start = 0, i = 0;
    for (char c: s) {
        if (c == '"') {
            in_quote = !in_quote;
        } else if (!in_quote && c == sep) {
            if (i > start)
                tokens.push_back(s.substr(start, i - start));
            start = i + 1;
        }
        ++i;
    }
    if (i > start)
        tokens.push_back(s.substr(start, i - start));
    return tokens;
}

bool TWPartition::Process_Fstab_Line(const char *fstab_line, bool Display_Error,
                                     std::map<std::string, Flags_Map> *twrp_flags) {
    std::string_view line(fstab_line);
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

    const std::vector<std::string> tokens = Split_Fstab_Line(line);
    std::string twflags;
    TWPartition *additional_entry = nullptr;

    for (size_t i = 0; i < tokens.size(); i++) {
        const std::string &tok = tokens[i];
        if (i == mount_point_index) {
            Mount_Point = tok;
            if (fstab_version == 2 && !Is_Super) {
                additional_entry = PartitionManager.Find_Partition_By_Path(Mount_Point);
                if (additional_entry)
                    LOGINFO("Found an additional entry for '%s'\n", Mount_Point.c_str());
            }
            LOGINFO("Processing '%s'\n", Mount_Point.c_str());
            Backup_Path = Mount_Point;
            Storage_Path = Mount_Point;
            Display_Name = tok.substr(1);
            Backup_Display_Name = Display_Name;
            Storage_Name = Display_Name;
        } else if (i == fs_index) {
            // File System
            Fstab_File_System = tok;
            Current_File_System = tok;
        } else if (i == block_device_index) {
            // Primary Block Device
            Primary_Block_Device = tok;
            if (tok.starts_with('/'))
                Find_Real_Block_Device(Primary_Block_Device, Display_Error);
        } else if (i > 2) {
            if (fstab_version == 2) {
                if (i == 3) {
                    Process_FS_Flags(tok);
                    if (additional_entry) {
                        additional_entry->Save_FS_Flags(Fstab_File_System, Mount_Flags, Mount_Options);
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
                    Alternate_Block_Device = tok;
                    Find_Real_Block_Device(Alternate_Block_Device, Display_Error);
                } else if (tok.size() > 7 && tok.starts_with("length=")) {
                    // Partition length
                    Length = atoi(tok.c_str() + 7);
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

    Override_Block_Devices_From_Flags(fstab_version, Display_Error, twrp_flags);
    Apply_Block_Device_Attributes(line);

    if (!Classify_By_Mount_Point(Display_Error)) return false;

    // Process TWRP fstab flags
    if (!twflags.empty()) {
        std::string Prev_Display_Name = Display_Name;
        std::string Prev_Storage_Name = Storage_Name;
        std::string Prev_Backup_Display_Name = Backup_Display_Name;
        Display_Name = "";
        Storage_Name = "";
        Backup_Display_Name = "";

        Process_TW_Flags(twflags, fstab_version == 1, fstab_version);
        Save_FS_Flags(Fstab_File_System, Mount_Flags, Mount_Options);

        bool has_display_name = !Display_Name.empty();
        bool has_storage_name = !Storage_Name.empty();
        bool has_backup_name = !Backup_Display_Name.empty();
        if (!has_display_name) Display_Name = Prev_Display_Name;
        if (!has_storage_name) Storage_Name = Prev_Storage_Name;
        if (!has_backup_name) Backup_Display_Name = Prev_Backup_Display_Name;

        if (has_display_name && !has_storage_name)
            Storage_Name = Display_Name;
        if (!has_display_name && has_storage_name)
            Display_Name = Storage_Name;
        if (has_display_name && !has_backup_name && Backup_Display_Name != "Android Secure")
            Backup_Display_Name = Display_Name;
        if (!has_display_name && has_backup_name)
            Display_Name = Backup_Display_Name;
    }

    Apply_Twrp_Flags_File(fstab_version, Display_Error, twrp_flags);
    Mount_Persist_Root_If_Needed();

    return true;
}

void TWPartition::Override_Block_Devices_From_Flags(int fstab_version, bool Display_Error,
                                                    std::map<std::string, Flags_Map> *twrp_flags) {
    // override block devices from the v2 fstab with the ones we read from the twrp.flags file in case they are different
    if (fstab_version == 2 && twrp_flags && twrp_flags->size() > 0) {
        auto it = twrp_flags->find(Mount_Point);
        if (it != twrp_flags->end()) {
            if (!it->second.Primary_Block_Device.empty()) {
                Primary_Block_Device = it->second.Primary_Block_Device;
                Find_Real_Block_Device(Primary_Block_Device, Display_Error);
            }
            if (!it->second.Alternate_Block_Device.empty()) {
                Alternate_Block_Device = it->second.Alternate_Block_Device;
                Find_Real_Block_Device(Alternate_Block_Device, Display_Error);
            }
        }
    }
}

void TWPartition::Apply_Block_Device_Attributes(std::string_view line) {
    if (line.starts_with("/devices/")) {
        Sysfs_Entry = Primary_Block_Device;
        Primary_Block_Device = "";
        Is_Storage = true;
        Removable = true;
        Wipe_Available_in_GUI = true;
        Wildcard_Block_Device = true;
    }
    if (Primary_Block_Device.find("*") != std::string::npos)
        Wildcard_Block_Device = true;
}

bool TWPartition::Classify_By_Mount_Point(bool Display_Error) {
    auto set_display_names = [this](const std::string &name) {
        Display_Name = name;
        Backup_Display_Name = name;
        Storage_Name = name;
    };

    if (Mount_Point == "auto") {
        Mount_Point = "/auto";
        Mount_Point += std::to_string(auto_index);
        Backup_Path = Mount_Point;
        Storage_Path = Mount_Point;
        Backup_Name = Mount_Point.substr(1);
        auto_index++;
        Setup_File_System(Display_Error);
        set_display_names("Storage");
        Can_Be_Backed_Up = false;
        Wipe_Available_in_GUI = true;
        Is_Storage = true;
        Removable = true;
        Wipe_Available_in_GUI = true;
    } else if (!Is_File_System(Fstab_File_System) && !Is_Image(Fstab_File_System)) {
        if (Display_Error)
            LOGERR("Unknown File System: '%s'\n", Fstab_File_System.c_str());
        else
            LOGINFO("Unknown File System: '%s'\n", Fstab_File_System.c_str());
        return false;
    } else if (Is_File_System(Fstab_File_System)) {
        Find_Actual_Block_Device();
        Setup_File_System(Display_Error);
        Backup_Name = Display_Name = Mount_Point.substr(1);
        if (Mount_Point == "/" || Mount_Point == "/system" || Mount_Point == "/system_root") {
            Mount_Point = PartitionManager.Get_Android_Root_Path();
            Backup_Path = Mount_Point;
            Storage_Path = Mount_Point;
            set_display_names("System");
            Backup_Name = "system";
            Wipe_Available_in_GUI = false;
            Can_Be_Backed_Up = false;
            Can_Be_Wiped = false;
            Make_Dir(PartitionManager.Get_Android_Root_Path(), true);
        } else if (Mount_Point == "/system_ext") {
            set_display_names("System_EXT");
            Backup_Name = "System_EXT";
            Can_Be_Backed_Up = Wipe_Available_in_GUI = !Is_Super;
        } else if (Mount_Point == "/product") {
            set_display_names("Product");
            Backup_Name = "Product";
            Can_Be_Backed_Up = Wipe_Available_in_GUI = !Is_Super;
        } else if (Mount_Point == "/odm") {
            set_display_names("ODM");
            Backup_Name = "ODM";
            Can_Be_Backed_Up = Wipe_Available_in_GUI = !Is_Super;
        } else if (Mount_Point == "/data") {
            set_display_names("Data");
            Wipe_Available_in_GUI = true;
            Wipe_During_Factory_Reset = true;
            Can_Be_Backed_Up = true;
            Can_Encrypt_Backup = true;
            Use_Userdata_Encryption = true;
        } else if (Mount_Point == "/cache") {
            set_display_names("Cache");
            Wipe_Available_in_GUI = true;
            Wipe_During_Factory_Reset = true;
            Can_Be_Backed_Up = true;
        } else if (Mount_Point == "/datadata") {
            Wipe_During_Factory_Reset = true;
            set_display_names("DataData");
            Is_SubPartition = true;
            SubPartition_Of = "/data";
            DataManager::SetValue(TW_HAS_DATADATA, 1);
            Can_Be_Backed_Up = true;
            Can_Encrypt_Backup = true;
            Use_Userdata_Encryption = false; // This whole partition should be encrypted
        } else if (Mount_Point == "/sd-ext") {
            Wipe_During_Factory_Reset = true;
            set_display_names("SD-Ext");
            Wipe_Available_in_GUI = true;
            Removable = true;
            Can_Be_Backed_Up = true;
            Can_Encrypt_Backup = true;
            Use_Userdata_Encryption = true;
        } else if (Mount_Point == "/boot") {
            Display_Name = "Boot";
            Backup_Display_Name = Display_Name;
            DataManager::SetValue("tw_boot_is_mountable", 1);
            Can_Be_Backed_Up = true;
        } else if (Mount_Point == "/vendor") {
            set_display_names("Vendor");
        } else if (Mount_Point == "/metadata") {
            set_display_names("Metadata");
        } else if (Mount_Point == "/odm_dlkm") {
            set_display_names("ODM DLKM");
        } else if (Mount_Point == "/vendor_dlkm") {
            set_display_names("Vendor DLKM");
        }
#ifdef TW_EXTERNAL_STORAGE_PATH
        if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
            Is_Storage = true;
            Storage_Path = EXPAND(TW_EXTERNAL_STORAGE_PATH);
            Removable = true;
            Wipe_Available_in_GUI = true;
#else
        if (Mount_Point == "/sdcard" || Mount_Point == "/external_sd" || Mount_Point == "/external_sdcard") {
            Is_Storage = true;
            Removable = true;
            Wipe_Available_in_GUI = true;
#endif
        }
#ifdef TW_INTERNAL_STORAGE_PATH
        if (Mount_Point == EXPAND(TW_INTERNAL_STORAGE_PATH)) {
            Is_Storage = true;
            Is_Settings_Storage = true;
            Storage_Path = EXPAND(TW_INTERNAL_STORAGE_PATH);
            Wipe_Available_in_GUI = true;
        }
#else
        if (Mount_Point == "/emmc" || Mount_Point == "/internal_sd" || Mount_Point == "/internal_sdcard") {
            Is_Storage = true;
            Is_Settings_Storage = true;
            Wipe_Available_in_GUI = true;
        }
#endif
    } else if (Is_Image(Fstab_File_System)) {
        Find_Actual_Block_Device();
        Setup_Image();
        if (Mount_Point == "/boot") {
            Display_Name = "Boot";
            Backup_Display_Name = Display_Name;
            Can_Be_Backed_Up = true;
            Can_Flash_Img = true;
        } else if (Mount_Point == "/init_boot") {
            Display_Name = "Init Boot";
            Backup_Display_Name = Display_Name;
            Can_Be_Backed_Up = true;
            Can_Flash_Img = true;
        } else if (Mount_Point == "/recovery") {
            Display_Name = "Recovery";
            Backup_Display_Name = Display_Name;
            Can_Flash_Img = true;
        } else if (Mount_Point == "/system_image") {
            Display_Name = "System Image";
            Backup_Display_Name = Display_Name;
            Can_Flash_Img = true;
            Can_Be_Backed_Up = true;
        } else if (Mount_Point == "/vendor_image") {
            Display_Name = "Vendor Image";
            Backup_Display_Name = Display_Name;
            Can_Flash_Img = true;
            Can_Be_Backed_Up = true;
        }
    }
    return true;
}

void TWPartition::Apply_Twrp_Flags_File(int fstab_version, bool Display_Error,
                                        std::map<std::string, Flags_Map> *twrp_flags) {
    if (fstab_version == 2 && twrp_flags && twrp_flags->size() > 0) {
        auto it = twrp_flags->find(Mount_Point);
        if (it != twrp_flags->end()) {
            std::string_view flagstr = it->second.Flags;
            constexpr std::string_view flags_prefix = "flags=";
            if (flagstr.size() > flags_prefix.size() && flagstr.starts_with(flags_prefix))
                flagstr.remove_prefix(flags_prefix.size());
            Process_TW_Flags(flagstr, Display_Error, 1);
            // Forcing the fstab to ver 1 because this data is coming from the /etc/twrp.flags which should be using the TWRP v1 flags format
        }
    }
}

void TWPartition::Mount_Persist_Root_If_Needed() {
    if (Mount_Point == TW_PERSIST_ROOT && Can_Be_Mounted) {
        bool mounted = Mount(false);
        if (mounted) {
            TWFunc::Fixup_Time_On_Boot(TW_PERSIST_ROOT "/time/");
            UnMount(false);
        }
    }
}

void TWPartition::Partition_Post_Processing(bool Display_Error) {
    if (Mount_Point == "/data")
        Setup_Data_Partition(Display_Error);
    else if (Mount_Point == "/cache")
        Setup_Cache_Partition(Display_Error);
}

void TWPartition::ExcludeAll(const std::string &path) {
    backup_exclusions.add_absolute_dir(path);
    wipe_exclusions.add_absolute_dir(path);
}

void TWPartition::Setup_Data_Partition(bool Display_Error) {
    if (Mount_Point != "/data")
        return;

    // Ensure /data is not mounted as tmpfs for qcom hardware decrypt
    UnMount(false);

#ifdef TW_INCLUDE_CRYPTO
#ifdef TW_PREPARE_DATA_MEDIA_EARLY
    if (datamedia)
        Setup_Data_Media();
#endif
    Can_Be_Encrypted = true;
    std::string crypto_blkdev = android::base::GetProperty("ro.crypto.fs_crypto_blkdev", "error");
    if (crypto_blkdev != "error") {
        Set_FBE_Status();
        Decrypted_Block_Device = crypto_blkdev;
        LOGINFO("Data already decrypted, new block device: '%s'\n", crypto_blkdev.c_str());
#ifndef TW_PREPARE_DATA_MEDIA_EARLY
    if (datamedia)
        Setup_Data_Media();
#endif
    DataManager::SetValue(TW_IS_ENCRYPTED, 0);
    } else if (!Mount(false)) {
        //		if (Is_Present) {
        //			if (Key_Directory.empty()) {
        //				set_partition_data(Use_Original_Path ? Original_Path.c_str() : Actual_Block_Device.c_str(), Crypto_Key_Location.c_str());
        //				if (cryptfs_check_footer() == 0) {
        //					Is_Encrypted = true;
        //					Is_Decrypted = false;
        //					Can_Be_Mounted = false;
        //					Current_File_System = "emmc";
        //					Setup_Image();
        //					DataManager::SetValue(TW_CRYPTO_PWTYPE, cryptfs_get_password_type());
        //					DataManager::SetValue("tw_crypto_pwtype_0", cryptfs_get_password_type());
        //					DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
        //					DataManager::SetValue("tw_crypto_display", "");
        //					if (datamedia)
        //						Setup_Data_Media();
        //				} else {
        //					gui_err("mount_data_footer=Could not mount /data and unable to find crypto footer.");
        //				}
        //			} else {
        Is_Encrypted = true;
        Is_Decrypted = false;
        if (datamedia)
            Setup_Data_Media();
        //			}
        if (Key_Directory.empty()) {
            LOGERR("Primary block device '%s' for mount point '%s' is not present!\n",
                   Primary_Block_Device.c_str(), Mount_Point.c_str());
        }
    } else {
        Set_FBE_Status();
        int is_device_fbe;
        DataManager::GetValue(TW_IS_FBE, is_device_fbe);
        std::string crypto_state = android::base::GetProperty("ro.crypto.state", "error");
        if (!Decrypt_FBE_DE() && crypto_state != "error") {
            if (is_device_fbe == 1)
                LOGERR("Unable to decrypt FBE device\n");
        } else {
            DataManager::SetValue(TW_IS_ENCRYPTED, 0);
#ifndef TW_PREPARE_DATA_MEDIA_EARLY
    if (datamedia)
        Setup_Data_Media();
#endif
        }
    }
    if (datamedia && (!Is_Encrypted || (Is_Encrypted && Is_Decrypted))) {
        Setup_Data_Media();
        Recreate_Media_Folder();
    }
#else
    if (datamedia) {
        Setup_Data_Media();
        Recreate_Media_Folder();
    }
#endif
}

void TWPartition::Set_FBE_Status() {
    DataManager::SetValue(TW_IS_DECRYPTED, 1);
    Is_Encrypted = true;
    Is_Decrypted = true;
    if (Key_Directory.empty()) {
        Is_FBE = false;
        DataManager::SetValue(TW_IS_FBE, 0);
    } else {
        LOGINFO("Setup_Data_Partition::Key_Directory::%s\n", Key_Directory.c_str());
        Is_FBE = true;
        DataManager::SetValue(TW_IS_FBE, 1);
    }
}

bool TWPartition::Decrypt_FBE_DE() {
    if (TWFunc::Path_Exists("/data/unencrypted/key/version")) {
        DataManager::SetValue(TW_IS_FBE, 1);
        PartitionManager.Set_Crypto_State();
        PartitionManager.Set_Crypto_Type("file");
        LOGINFO("File Based Encryption is present\n");
#ifdef TW_INCLUDE_FBE
        Is_FBE = true;
        ExcludeAll(Mount_Point + "/convert_fbe");
        ExcludeAll(Mount_Point + "/unencrypted");
        ExcludeAll(Mount_Point + "/misc/vold/user_keys");
        ExcludeAll(Mount_Point + "/misc/vold/volume_keys");
        ExcludeAll(Mount_Point + "/system/gatekeeper.password.key");
        ExcludeAll(Mount_Point + "/system/gatekeeper.pattern.key");
        ExcludeAll(Mount_Point + "/system/locksettings.db");
        ExcludeAll(Mount_Point + "/system/locksettings.db-wal");
        ExcludeAll(Mount_Point + "/misc/gatekeeper");
        ExcludeAll(Mount_Point + "/misc/keystore");
        ExcludeAll(Mount_Point + "/drm/kek.dat");
        ExcludeAll(Mount_Point + "/system_de/0/spblob"); // contains data needed to decrypt synthetic password
        ExcludeAll(Mount_Point + "/system/users/0/gatekeeper.password.key");
        ExcludeAll(Mount_Point + "/system/users/0/gatekeeper.pattern.key");
        ExcludeAll(Mount_Point + "/cache");
        ExcludeAll(Mount_Point + "/per_boot"); // removed each boot by init
        ExcludeAll(Mount_Point + "/gsi"); // cow devices

        // Not retried: every attempt installs keys and touches the key directories
        // again, so a retry only widens the damage when the first one went wrong.
        if (android::keystore::Decrypt_DE()) {
            PartitionManager.Set_Crypto_State();
            Is_Encrypted = true;
            Is_Decrypted = false;
            DataManager::SetValue(TW_IS_ENCRYPTED, 1);
            std::string filename;
            int pwd_type = android::keystore::Get_Password_Type(0, filename);
            if (pwd_type < 0) {
                LOGERR("This TWRP does not have synthetic password decrypt support\n");
                pwd_type = 0; // default password
            }
            PartitionManager.Parse_Users(); // after load_all_de_keys() to parse_users
            std::vector<users_struct> *userList = PartitionManager.Get_Users_List();
            for (const users_struct &user: *userList) {
                if (atoi(user.userId.c_str()) != 0) {
                    ExcludeAll(Mount_Point + "/system_de/" + user.userId + "/spblob");
                    ExcludeAll(Mount_Point + "/system/users/" + user.userId + "/gatekeeper.password.key");
                    ExcludeAll(Mount_Point + "/system/users/" + user.userId + "/gatekeeper.pattern.key");
                    ExcludeAll(Mount_Point + "/system/users/" + user.userId + "/locksettings.db");
                    ExcludeAll(Mount_Point + "/system/users/" + user.userId + "/locksettings.db-wal");
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

void TWPartition::Setup_Cache_Partition(bool Display_Error __unused) {
    if (Mount_Point != "/cache") return;

    if (!Mount(true)) return;

    if (!TWFunc::Path_Exists("/cache/recovery/.")) {
        LOGINFO("Recreating /cache/recovery folder\n");
        if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
            LOGERR("Could not create /cache/recovery\n");
    }
}

void TWPartition::Process_FS_Flags(std::string_view str) {
    Mount_Options = "";

    // Avoid issues with potentially nested strtok by using Tokenize, which
    // coalesces runs of delimiter bytes and ignores leading/trailing ones
    // (matching the legacy strtok_r(options, ",", ...) semantics exactly).
    for (const std::string &tok: android::base::Tokenize(std::string(str), ",")) {
        auto equals = tok.find('=');
        size_t name_len = equals == std::string::npos ? tok.size() : equals;

        // Skip flags TWRP ignores. The legacy strncmp(tok, *ignored, name_len)
        // is a PREFIX test on the name part (before '='), NOT full equality, so
        // substr(0, name_len) clamps to the shorter side (tokens and ignored
        // items have no embedded NULs, mirroring strncmp's null-stop behavior).
        if (std::ranges::any_of(ignored_mount_items, [&](const std::string &ignored_mount_item) {
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
            auto it = std::ranges::find_if(mount_flags, [&](const flag_list &mount_flag) {
                return mount_flag.name && tok == mount_flag.name;
            });
            if (it != std::ranges::end(mount_flags)) {
                if (it->flag == MS_RDONLY)
                    Mount_Read_Only = true;
                else
                    Mount_Flags |= it->flag;
                continue;
            }
        }

        // If we aren't ignoring this flag and it's not a mount flag, then it must be a mount option
        if (!Mount_Options.empty())
            Mount_Options += ",";
        Mount_Options += tok;
    }
}

void TWPartition::Save_FS_Flags(const std::string &local_File_System, int local_Mount_Flags,
                                const std::string &local_Mount_Options) {
    partition_fs_flags_struct flags;
    flags.File_System = local_File_System;
    flags.Mount_Flags = local_Mount_Flags;
    flags.Mount_Options = local_Mount_Options;
    fs_flags.push_back(flags);
}

void TWPartition::Apply_TW_Flag(const unsigned flag, std::string_view str, const bool val) {
    switch (flag) {
        case TWFLAG_ANDSEC:
            Has_Android_Secure = val;
            break;
        case TWFLAG_BACKUP:
            Can_Be_Backed_Up = val;
            break;
        case TWFLAG_BACKUPNAME:
            Backup_Display_Name = str;
            break;
        case TWFLAG_BLOCKSIZE:
            Format_Block_Size = static_cast<unsigned long>(atol(std::string(str).c_str()));
            break;
        case TWFLAG_CANBEWIPED:
            Can_Be_Wiped = val;
            break;
        case TWFLAG_CANENCRYPTBACKUP:
            Can_Encrypt_Backup = val;
            break;
        case TWFLAG_DEFAULTS:
        case TWFLAG_WAIT:
        case TWFLAG_VERIFY:
        case TWFLAG_CHECK:
        case TWFLAG_NOTRIM:
        case TWFLAG_VOLDMANAGED:
        case TWFLAG_RESIZE:
            // Do nothing
            break;
        case TWFLAG_DISPLAY:
            Display_Name = str;
            break;
        case TWFLAG_ENCRYPTABLE:
        case TWFLAG_FORCEENCRYPT:
            Crypto_Key_Location = str;
            break;
        case TWFLAG_FILEENCRYPTION:
            // This flag isn't used by TWRP but is needed in 9.0 FBE decrypt
            // fileencryption=ice:aes-256-heh
        {
            std::string FBE(str);
            size_t colon_loc = FBE.find(":");
            if (colon_loc == std::string::npos) {
                android::base::SetProperty("fbe.contents", FBE);
                android::base::SetProperty("fbe.filenames", "");
                LOGINFO("FBE contents '%s', filenames ''\n", FBE.c_str());
                break;
            }
            std::string FBE_contents, FBE_filenames;
            FBE_contents = FBE.substr(0, colon_loc);
            FBE_filenames = FBE.substr(colon_loc + 1);
            android::base::SetProperty("fbe.contents", FBE_contents);
            android::base::SetProperty("fbe.filenames", FBE_filenames);
            LOGINFO("FBE contents '%s', filenames '%s'\n", FBE_contents.c_str(), FBE_filenames.c_str());
        }
        break;
        case TWFLAG_METADATA_ENCRYPTION:
            // This flag isn't used by TWRP but is needed for FBEv2 metadata decryption
            // metadata_encryption=aes-256-xts:wrappedkey_v0
        {
            std::string META(str);
            size_t colon_loc = META.find(":");
            if (colon_loc == std::string::npos) {
                android::base::SetProperty("metadata.contents", META);
                android::base::SetProperty("metadata.filenames", "");
                LOGINFO("Metadata contents '%s', filenames ''\n", META.c_str());
                break;
            }
            std::string META_contents, META_filenames;
            META_contents = META.substr(0, colon_loc);
            META_filenames = META.substr(colon_loc + 1);
            android::base::SetProperty("metadata.contents", META_contents);
            android::base::SetProperty("metadata.filenames", META_filenames);
            LOGINFO("Metadata contents '%s', filenames '%s'\n", META_contents.c_str(), META_filenames.c_str());
        }
        break;
        case TWFLAG_WRAPPEDKEY:
            // no more processing needed. leaving it here in case we want to do something in the future
            break;
        case TWFLAG_FLASHIMG:
            Can_Flash_Img = val;
            break;
        case TWFLAG_FSFLAGS:
            Process_FS_Flags(str);
            break;
        case TWFLAG_IGNOREBLKID:
            Ignore_Blkid = val;
            break;
        case TWFLAG_LENGTH:
            Length = atoi(std::string(str).c_str());
            break;
        case TWFLAG_MOUNTTODECRYPT:
            Mount_To_Decrypt = val;
            break;
        case TWFLAG_QUOTA:
            // Filesystem flag - TWRP does not need to process
            break;
        case TWFLAG_REMOVABLE:
            Removable = val;
            break;
        case TWFLAG_SETTINGSSTORAGE:
            Is_Settings_Storage = val;
            if (Is_Settings_Storage)
                Is_Storage = true;
            break;
        case TWFLAG_STORAGE:
            Is_Storage = val;
            break;
        case TWFLAG_STORAGENAME:
            Storage_Name = str;
            break;
        case TWFLAG_SUBPARTITIONOF:
            Is_SubPartition = true;
            SubPartition_Of = str;
            break;
        case TWFLAG_SYMLINK:
            Symlink_Path = str;
            break;
        case TWFLAG_USERDATAENCRYPTBACKUP:
            Use_Userdata_Encryption = val;
            if (Use_Userdata_Encryption)
                Can_Encrypt_Backup = true;
            break;
        case TWFLAG_USERMRF:
            Use_Rm_Rf = val;
            break;
        case TWFLAG_WIPEDURINGFACTORYRESET:
            Wipe_During_Factory_Reset = val;
            if (Wipe_During_Factory_Reset) {
                Can_Be_Wiped = true;
                Wipe_Available_in_GUI = true;
            }
            break;
        case TWFLAG_WIPEINGUI:
        case TWFLAG_FORMATTABLE:
            Wipe_Available_in_GUI = val;
            if (Wipe_Available_in_GUI)
                Can_Be_Wiped = true;
            break;
        case TWFLAG_SLOTSELECT:
            SlotSelect = true;
            break;
        case TWFLAG_ALTDEVICE:
            Alternate_Block_Device = str;
            break;
        case TWFLAG_ADOPTED_MOUNT_DELAY:
            Adopted_Mount_Delay = atoi(std::string(str).c_str());
            break;
        case TWFLAG_KEYDIRECTORY:
            Key_Directory = str;
            LOGINFO("setting Key_Directory to: %s\n", Key_Directory.c_str());
            break;
        case TWFLAG_DM_USE_ORIGINAL_PATH:
            Use_Original_Path = true;
            break;
        case TWFLAG_LOGICAL:
            Is_Super = true;
            break;
        case TWFLAG_FS_COMPRESS:
#ifdef TW_ENABLE_FS_COMPRESSION
            Needs_Fs_Compress = true;
            LOGINFO("Enabling 'fs compression'\n");
#else
            LOGINFO("Ignoring the 'fscompress' fstab flag\n");
#endif
            break;
        case TWFLAG_METADATA_CSUM:
            Needs_Metadata_Csum = true;
            break;
        default:
            // Should not get here
            LOGINFO("Flag identified for processing, but later unmatched: %i\n", flag);
            break;
    }
}

void TWPartition::Process_TW_Flags(std::string_view flags, bool Display_Error, int fstab_ver) {
    // Semicolons within double-quotes are not separators, so split on `sep`
    // with quote awareness (mirrors the legacy separator->'\n' mark + strtok_r:
    // a sep inside "..." is kept, the '"' is kept in the token for the caller
    // to strip, and consecutive/leading/trailing separators are coalesced).
    const char sep = fstab_ver == 2 ? ',' : ';';
    for (std::string_view t: Split_Quoted(flags, sep)) {
        const flag_list *tw_flag = tw_flags;

        for (; tw_flag->name; ++tw_flag) {
            std::string_view name(tw_flag->name);
            const size_t flag_len = name.size();
            if (!t.starts_with(name))
                continue;

            // Reject prefix collisions: a bare flag whose name is a proper
            // prefix of the token but is not followed by '='
            // (e.g. "backup" vs "backupname=", "storage" vs "storagename=").
            if (t.size() > flag_len && !name.ends_with('=') && t[flag_len] != '=')
                continue;

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

            Apply_TW_Flag(tw_flag->flag, value, flag_val);
            break;
        }
        if (!tw_flag->name) {
            if (Display_Error)
                LOGERR("Unhandled flag: '%s'\n", std::string(t).c_str());
            else
                LOGINFO("Unhandled flag: '%s'\n", std::string(t).c_str());
        }
    }
}

constexpr std::array<std::string_view, 11> fs_list = {
    "ext2", "ext3", "ext4", "vfat",
    "ntfs", "exfat", "f2fs", "mifs",
    "erofs", "squashfs", "auto",
};

bool TWPartition::Is_File_System(std::string File_System) {
    return std::ranges::find(fs_list, File_System) != fs_list.end();
}

bool TWPartition::Is_Image(std::string File_System) {
    return File_System == "emmc";
}

bool TWPartition::Make_Dir(std::string Path, bool Display_Error) {
    if (TWFunc::Get_D_Type_From_Stat(Path) != S_IFDIR)
        unlink(Path.c_str());
    if (!TWFunc::Path_Exists(Path)) {
        if (mkdir(Path.c_str(), 0777) == -1) {
            if (Display_Error)
                gui_msg(
                    Msg(msg::kError, "create_folder_strerr=Can not create '{1}' folder ({2}).")(Path)(strerror(errno)));
            else
                LOGINFO("Can not create '%s' folder.\n", Path.c_str());
            return false;
        }
        LOGINFO("Created '%s' folder.\n", Path.c_str());
        return true;
    }
    return true;
}

void TWPartition::Setup_File_System(bool Display_Error) {
    Can_Be_Mounted = true;
    Can_Be_Wiped = true;

    // Make the mount point folder if it doesn't exist
    Make_Dir(Mount_Point, Display_Error);
    Backup_Method = BackupMethod::BM_FILES;
}

void TWPartition::Setup_Image() {
    Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
    Backup_Name = Display_Name;
    if (Current_File_System == "emmc")
        Backup_Method = BackupMethod::BM_DD;
    else
        LOGINFO("Unhandled file system '%s' on image '%s'\n", Current_File_System.c_str(), Display_Name.c_str());
}

void TWPartition::Setup_AndSec() {
    Backup_Display_Name = "Android Secure";
    Backup_Name = "and-sec";
    Can_Be_Backed_Up = true;
    Has_Android_Secure = true;
    Symlink_Path = Mount_Point + "/.android_secure";
    Symlink_Mount_Point = "/and-sec";
    Backup_Path = Symlink_Mount_Point;
    Make_Dir("/and-sec", true);
    Recreate_AndSec_Folder();
    Mount_Storage_Retry(true);
}

void TWPartition::Setup_Data_Media() {
    LOGINFO("Setting up '%s' as data/media emulated storage.\n", Mount_Point.c_str());
    if (Storage_Name.empty() || Storage_Name == "Data")
        Storage_Name = "Internal Storage";
    Has_Data_Media = true;
    Is_Storage = true;
    Storage_Path = Mount_Point + "/media";
    Symlink_Path = Storage_Path;
    if (Mount_Point == "/data") {
        Is_Settings_Storage = true;
        if (std::string_view(EXPAND(TW_EXTERNAL_STORAGE_PATH)) == "/sdcard") {
            Make_Dir("/emmc", false);
            Symlink_Mount_Point = "/emmc";
        } else {
            Make_Dir("/sdcard", false);
            Symlink_Mount_Point = "/sdcard";
        }
        const std::string media0 = Mount_Point + "/media/0";
        if (bool mount = Mount(false);
#ifdef TW_PREPARE_DATA_MEDIA_EARLY
            mount &&
#endif
            TWFunc::Path_Exists(media0)) {
            Storage_Path = media0;
            Symlink_Path = Storage_Path;
            DataManager::SetValue(TW_INTERNAL_PATH, media0);
#ifndef TW_INCLUDE_CRYPTO
            DataManager::SetValue("tw_settings_path", TW_STORAGE_PATH);
#endif
            UnMount(true);
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
        backup_exclusions.add_absolute_dir("/data/extm"); //exclude this dir to prevent "error 255" on MIUI
        backup_exclusions.add_absolute_dir("/data/gsi");
        // Contains huge files (DSU System image + Userdata image), and won't work after restoration (requires configuration files in metadata)
        backup_exclusions.add_absolute_dir("/data/adb/ksu/modules.img");
        //After ksu 0.8.x the modules.img file became 1tb, which is inhibiting the execution of backups
        wipe_exclusions.add_absolute_dir(Mount_Point + "/misc/vold"); // adopted storage keys
        ExcludeAll(Mount_Point + "/system/storage.xml");
#ifdef TW_WORKAROUND_BACKUP_BUG
        backup_exclusions.add_absolute_dir("/data/data");
        // temporary workaround for error 255 when restoring data backups in 16 branch builds
#endif

        backup_exclusions.add_absolute_dir("/data/system/users/0/package-restrictions.xml");

        // board-customisable exclusions
#ifdef TW_BACKUP_EXCLUSIONS
        for (const std::string &extra_x: android::base::Tokenize(TW_BACKUP_EXCLUSIONS, ",")) {
            std::string s1 = android::base::Trim(extra_x);
            if (!s1.empty()) {
                backup_exclusions.add_absolute_dir(s1);
                LOGINFO("Adding user-defined path '%s' to the backup exclusions\n", s1.c_str());
            }
        }
#endif
    } else {
        for (int i: std::views::iota(2, 10)) {
            std::string path = "/sdcard" + std::to_string(i);
            if (!TWFunc::Path_Exists(path)) {
                Make_Dir(path, false);
                Symlink_Mount_Point = path;
                LOGINFO("'%s' data/media emulated storage symlinked to %s.\n", Mount_Point.c_str(),
                        Symlink_Mount_Point.c_str());
                break;
            }
        }
        if (Mount(true) && TWFunc::Path_Exists(Mount_Point + "/media/0")) {
            Storage_Path = Mount_Point + "/media/0";
            Symlink_Path = Storage_Path;
            UnMount(true);
        }
    }
    ExcludeAll(Mount_Point + "/media");
}

void TWPartition::Find_Real_Block_Device(std::string &Block, bool Display_Error) {
    Original_Path = Block;

    std::string device = Block;
    char realDevice[PATH_MAX];
    while (true) {
        ssize_t n = readlink(device.c_str(), realDevice, sizeof(realDevice) - 1);
        if (n <= 0)
            break;
        realDevice[n] = '\0';
        device = realDevice;
    }

    Block = device;
}

bool TWPartition::Mount_Storage_Retry(bool Display_Error) {
    // On some devices, storage doesn't want to mount right away, retry and sleep
    if (!Mount(Display_Error)) {
        int retry_count = 5;
        while (retry_count > 0 && !Mount(false)) {
            usleep(500000);
            retry_count--;
        }
        return Mount(Display_Error);
    }
    return true;
}

bool TWPartition::Get_Size_Via_statfs(bool Display_Error) {
    if (!Mount(Display_Error))
        return false;

    struct statfs st;
    std::filesystem::path local_path = std::filesystem::path(Mount_Point);
    if (statfs(local_path.c_str(), &st) != 0) {
        if (!Removable) {
            if (Display_Error)
                LOGERR("Unable to statfs '%s'\n", local_path.c_str());
            else
                LOGINFO("Unable to statfs '%s'\n", local_path.c_str());
        }
        return false;
    }
    Size = st.f_blocks * st.f_bsize;
    Used = (st.f_blocks - st.f_bfree) * st.f_bsize;
    Free = st.f_bfree * st.f_bsize;
    Backup_Size = Used;
    return true;
}

bool TWPartition::Get_Size_Via_df(bool Display_Error) {
    if (!Mount(Display_Error))
        return false;

    // df default output, one row per filesystem with sizes in 1K-blocks:
    //   Filesystem 1K-blocks Used Available Use% Mounted on
    //   <device>  <size>   <used> <avail>  <%>  <mountpoint>
    // When the device name overflows its column, df writes the name alone on one
    // line and the numeric fields on an indented continuation line.
    // Capture df's stdout directly through a pipe instead of a temp file: the
    // previous /tmp/dfoutput.txt was a fixed path that concurrent calls clobber.
    std::string output;
    std::string command = std::format("df {}", Mount_Point);
    // Exec_Cmd streams df's stdout into `output` via a pipe and already logs a
    // popen failure (LOGERR); an empty or unparseable result is caught by the
    // parse loop below, which returns false. Gating on the return code is
    // avoided because pclose() may return -1 (e.g. EINTR) after output was read.
    TWFunc::Exec_Cmd(command, output, false);

    std::string line;
    std::istringstream iss(output);
    while (std::getline(iss, line)) {
        if (line.starts_with("Filesystem"))
            continue;  // header row

        unsigned long blocks = 0, used = 0, available = 0;
        int parsed;
        // A continuation row is indented (the device column is empty), so it is
        // parsed from its first number. A normal row begins with the device
        // name, which %*s discards before the three numeric fields. The lone
        // device name on a wrapped row leaves no number for %lu to read, so it
        // falls through (parsed != 3) and its continuation is handled next loop.
        if (line.starts_with(' ') || line.starts_with('\t'))
            parsed = sscanf(line.c_str(), "%lu %lu %lu", &blocks, &used, &available);
        else
            parsed = sscanf(line.c_str(), "%*s %lu %lu %lu", &blocks, &used, &available);

        if (parsed != 3)
            continue;

        // df reports sizes in 1K-blocks; convert to bytes.
        Size = blocks * 1024ULL;
        Used = used * 1024ULL;
        Free = available * 1024ULL;
        Backup_Size = Used;
        return true;  // df returns a single row for this mount point
    }

    LOGINFO("Unable to parse df output for '%s'.\n", Mount_Point.c_str());
    return false;
}

unsigned long long TWPartition::IOCTL_Get_Block_Size() {
    Find_Actual_Block_Device();

    return TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
}

bool TWPartition::Find_Partition_Size() {
    unsigned long long ioctl_size = IOCTL_Get_Block_Size();
    if (ioctl_size) {
        Size = ioctl_size;
        return true;
    }

    return false;
}

bool TWPartition::Is_Mounted() {
    if (!Can_Be_Mounted)
        return false;

    // Check to see if the mount point directory exists
    struct stat st1;
    if (stat((Mount_Point + "/.").c_str(), &st1) != 0)
        return false;

    // Check to see if the directory above the mount point exists
    struct stat st2;
    if (stat((Mount_Point + "/../.").c_str(), &st2) != 0)
        return false;

    // Check to see if a symlink mount point exists and is mounted
    if (!Symlink_Mount_Point.empty()) {
        scan_mounted_volumes();
        const MountedVolume *sml = find_mounted_volume_by_mount_point(Symlink_Mount_Point.c_str());
        if (sml != nullptr)
            return true;
    }

    // Compare the device IDs -- if they match then we're (probably) using tmpfs instead of an actual device
    return st1.st_dev != st2.st_dev;
}

bool TWPartition::Is_File_System_Writable() {
    if (!Is_File_System(Current_File_System) || !Is_Mounted())
        return false;

    std::string test_path = Mount_Point + "/.";
    return (access(test_path.c_str(), W_OK) == 0);
}

bool TWPartition::Mount(bool Display_Error) {
    int exfat_mounted = 0;
    unsigned int flags = Mount_Flags;

    if (Is_Mounted()) {
        return true;
    } else if (!Can_Be_Mounted) {
        return false;
    }

    // Check the current file system before mounting
    Check_FS_Type();
    if (Current_File_System == "exfat") {
        std::string cmd = "/system/bin/mount -t exfat " + Actual_Block_Device + " " + Mount_Point;
        LOGINFO("cmd: %s\n", cmd.c_str());
        std::string result;
        if (TWFunc::Exec_Cmd(cmd, result, false) != 0) {
            LOGINFO("exfat failed to mount with result '%s', trying vfat\n", result.c_str());
            Current_File_System = "vfat";
        } else {
            exfat_mounted = 1;
        }
    }

    if (Current_File_System == "ntfs" && !TWFunc::Path_Exists("/sys/module/tntfs") && (
            TWFunc::Path_Exists("/system/bin/ntfs-3g") || TWFunc::Path_Exists("/system/bin/mount.ntfs"))) {
        std::string cmd;
        std::string Ntfsmount_Binary = "";

        if (TWFunc::Path_Exists("/system/bin/ntfs-3g"))
            Ntfsmount_Binary = "ntfs-3g";
        else if (TWFunc::Path_Exists("/system/bin/mount.ntfs"))
            Ntfsmount_Binary = "mount.ntfs";

        if (Mount_Read_Only)
            cmd = "/system/bin/" + Ntfsmount_Binary + " -o ro " + Actual_Block_Device + " " + Mount_Point;
        else
            cmd = "/system/bin/" + Ntfsmount_Binary + " " + Actual_Block_Device + " " + Mount_Point;
        LOGINFO("cmd: '%s'\n", cmd.c_str());

        if (TWFunc::Exec_Cmd(cmd) == 0) {
            return true;
        } else {
            LOGINFO("ntfs-3g failed to mount, trying regular mount method.\n");
        }
    } else {
        if (Current_File_System == "ntfs" && TWFunc::Path_Exists("/sys/module/tntfs"))
            Current_File_System = "tntfs";
    }

    if (Mount_Read_Only)
        flags |= MS_RDONLY;

    std::string mount_fs = Current_File_System;
    if (Current_File_System == "exfat" && TWFunc::Path_Exists("/sys/module/texfat"))
        mount_fs = "texfat";

    if (!exfat_mounted &&
        mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), mount_fs.c_str(), flags, Mount_Options.c_str()) != 0 &&
        mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), mount_fs.c_str(), flags, nullptr) != 0) {
        if (!Removable && Display_Error)
            gui_msg(Msg(msg::kError, "fail_mount=Failed to mount '{1}' ({2})")(Mount_Point)(strerror(errno)));
        else
            LOGINFO("Unable to mount '%s'\n", Mount_Point.c_str());

        LOGINFO("Actual block device: '%s', current file system: '%s'\n", Actual_Block_Device.c_str(),
                Current_File_System.c_str());
        return false;
    }

    if (Removable)
        Update_Size(Display_Error);

    if (!Symlink_Mount_Point.empty()) {
        if (!Bind_Mount(false))
            return false;
    }
    return true;
}

bool TWPartition::Bind_Mount(bool Display_Error) {
    if (TWFunc::Path_Exists(Symlink_Path)) {
        if (mount(Symlink_Path.c_str(), Symlink_Mount_Point.c_str(), "", MS_BIND, nullptr) < 0) {
            return false;
        }
    }
    return true;
}

void TWPartition::Ensure_Subdirectory_Unmounted(const char *Mount_Point) {
    std::unique_ptr<FILE, decltype(&endmntent)> mnts(setmntent("/proc/mounts", "r"), endmntent);
    if (!mnts) {
        LOGINFO("Could not read /proc/mounts\n");
        return;
    }

    // Find sudirectory mount point
    std::string top_directory(Mount_Point);
    if (!android::base::EndsWith(top_directory, "/")) {
        top_directory += "/";
    }

    std::vector<std::string> umount_points;
    mntent *mentry;
    while ((mentry = getmntent(mnts.get())) != nullptr) {
        if (top_directory == mentry->mnt_dir) {
            continue;
        }

        if (android::base::StartsWith(mentry->mnt_dir, top_directory)) {
            LOGINFO("Found sub-directory mount '%s' under '%s'\n", mentry->mnt_dir, Mount_Point);
            umount_points.emplace_back(mentry->mnt_dir);
        }
    }

    // Sort by path length to umount longest path first
    std::ranges::sort(umount_points, [](const std::string &s1, const std::string &s2) {
        return s1.length() > s2.length();
    });

    for (const auto &mount_point: umount_points) {
        LOGINFO("Unmounting sub-directory mount '%s'\n", mount_point.c_str());
        if (umount(mount_point.c_str()) != 0) {
            LOGINFO("Failed to unmount '%s': '%s'\n", mount_point.c_str(), strerror(errno));
        }
    }
}

bool TWPartition::UnMount(bool Display_Error, int flags) {
    if (Is_Mounted()) {
        int never_unmount_system;

        DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
        if (never_unmount_system == 1 && Mount_Point == PartitionManager.Get_Android_Root_Path())
            return true; // Never unmount system if you're not supposed to unmount it

        Ensure_Subdirectory_Unmounted(Mount_Point.c_str());

        if (Is_Storage && MTP_Storage_ID > 0)
            PartitionManager.Remove_MTP_Storage(MTP_Storage_ID);

        if (!Symlink_Mount_Point.empty())
            umount2(Symlink_Mount_Point.c_str(), flags);

        umount2(Mount_Point.c_str(), flags);
        if (Is_Mounted()) {
            if (Display_Error)
                gui_msg(Msg(msg::kError, "fail_unmount=Failed to unmount '{1}' ({2})")(Mount_Point)(strerror(errno)));
            else
                LOGINFO("Unable to unmount '%s'\n", Mount_Point.c_str());
            return false;
        } else {
            // fscrypt keys live in the mounted filesystem's keyring.
            if (Is_FBE && Mount_Point == "/data" && DataManager::GetIntValue(TW_IS_DECRYPTED))
                PartitionManager.Mark_Data_Locked();
            return true;
        }
    } else {
        return true;
    }
}

bool TWPartition::ReMount(bool Display_Error) {
    if (UnMount(Display_Error))
        return Mount(Display_Error);
    return false;
}

bool TWPartition::ReMount_RW(bool Display_Error) {
    // No need to remount if already mounted rw
    if (Is_File_System_Writable())
        return true;

    bool ro = Mount_Read_Only;
    int flags = Mount_Flags;

    Mount_Read_Only = false;
    Mount_Flags &= ~MS_RDONLY;

    bool ret = ReMount(Display_Error);

    Mount_Read_Only = ro;
    Mount_Flags = flags;

    return ret;
}

bool TWPartition::BlkDiscard() {
    std::string cmd;
    LOGINFO("Perform BLKDISCARD on block device %s\n", Actual_Block_Device.c_str());
    cmd = "/system/bin/toybox blkdiscard " + Actual_Block_Device;
    return (TWFunc::Exec_Cmd(cmd) == 0);
}

bool TWPartition::Wipe(std::string New_File_System) {
    bool wiped = false, update_crypt = false;
    int check;

    if (!Can_Be_Wiped) {
        gui_msg(Msg(msg::kError, "cannot_wipe=Partition {1} cannot be wiped.")(Display_Name));
        return false;
    }

    if (Mount_Point == "/cache") Log_Offset = 0;

    if (Mount_Point == PartitionManager.Get_Android_Root_Path()) {
        if (tw_get_default_metadata(PartitionManager.Get_Android_Root_Path().c_str()) != 0) {
            gui_msg(Msg(msg::kWarning,
                        "restore_system_context=Unable to get default context for {1} -- Android may not boot.")(
                PartitionManager.Get_Android_Root_Path()));
        }
    }

    if (Has_Data_Media && Current_File_System == New_File_System) {
        wiped = Wipe_Data_Without_Wiping_Media();
        if (Mount_Point == "/data" && TWFunc::get_log_dir() == DATA_LOGS_DIR) {
            bool created = PartitionManager.Recreate_Logs_Dir();
            if (!created)
                LOGERR("Unable to create log directory for TWRP\n");
        }
    } else {
        DataManager::GetValue(TW_RM_RF_VAR, check);
        if (check || Use_Rm_Rf)
            wiped = Wipe_RMRF();
        else if (New_File_System == "ext4")
            wiped = Wipe_EXT4();
        else if (New_File_System == "ext2" || New_File_System == "ext3")
            wiped = Wipe_EXTFS(New_File_System);
        else if (New_File_System == "exfat")
            wiped = Wipe_EXFAT();
        else if (New_File_System == "ntfs" || Current_File_System == "tntfs")
            wiped = Wipe_NTFS();
        else if (New_File_System == "f2fs" || Current_File_System == "mifs")
            wiped = Wipe_F2FS();
        else if (New_File_System == "vfat")
            wiped = Wipe_FAT();
        else {
            LOGERR("Unable to wipe '%s' -- unknown file system '%s'\n", Mount_Point.c_str(), New_File_System.c_str());
            return false;
        }
        update_crypt = false;
        // intentionally disabled: fscrypt (AOSP 10) made this crypt-refresh path obsolete; the `if (update_crypt)` block below is retained but inert.
    }

    if (wiped) {
        if (Mount_Point == "/cache" && TWFunc::get_log_dir() != DATA_LOGS_DIR)
            DataManager::Output_Version();

        if (Mount_Point == PartitionManager.Get_Android_Root_Path()) {
            tw_set_default_metadata(PartitionManager.Get_Android_Root_Path().c_str());
        }
        if (update_crypt) {
            Setup_File_System(false);
            if (Is_Encrypted && !Is_Decrypted) {
                // just wiped an encrypted partition back to its unencrypted state
                Is_Encrypted = false;
                Is_Decrypted = false;
                Decrypted_Block_Device = "";
                if (Mount_Point == "/data") {
                    DataManager::SetValue(TW_IS_ENCRYPTED, 0);
                    DataManager::SetValue(TW_IS_DECRYPTED, 0);
                }
            }
        }

        if (Is_Storage && Mount(false) && !Is_FBE)
            PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
    }

    return wiped;
}

bool TWPartition::Wipe() {
    if (Is_File_System(Current_File_System))
        return Wipe(Current_File_System);
    else
        return Wipe(Fstab_File_System);
}

bool TWPartition::Wipe_AndSec() {
    if (!Has_Android_Secure)
        return false;

    if (!Mount(true))
        return false;

    gui_msg(Msg("wiping=Wiping {1}")(Backup_Display_Name));
    TWFunc::removeDir(Mount_Point + "/.android_secure/", true);
    return true;
}

bool TWPartition::Wipe_Data_Cache() {
    if (!Mount(true))
        return false;
    gui_msg(Msg("wiping=Wiping {1}")(Mount_Point + "/cache/"));
    TWFunc::removeDir(Mount_Point + "/cache/", true);
    return true;
}

bool TWPartition::Can_Repair() {
    if (Mount_Read_Only)
        return false;
    if (Current_File_System == "vfat" && TWFunc::Path_Exists("/system/bin/fsck.fat"))
        return true;
    if ((Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") &&
        TWFunc::Path_Exists("/system/bin/e2fsck"))
        return true;
    if (Current_File_System == "exfat" && TWFunc::Path_Exists("/system/bin/fsck.exfat"))
        return true;
    if ((Current_File_System == "f2fs" || Current_File_System == "mifs") &&
        TWFunc::Path_Exists("/system/bin/fsck.f2fs"))
        return true;
    if ((Current_File_System == "ntfs" || Current_File_System == "tntfs") && (
            TWFunc::Path_Exists("/system/bin/ntfsfix") || TWFunc::Path_Exists("/system/bin/fsck.ntfs")))
        return true;
    return false;
}

bool TWPartition::Repair() {
    // Each filesystem's repair follows the same shape — verify the fsck binary exists, unmount,
    // run it, report — differing only in binary name and flags. ntfs/tntfs picks between
    // ntfsfix and fsck.ntfs at runtime, then calls in with no flags.
    auto do_repair = [&](const char *binary, const char *flags, const char *error_name) -> bool {
        std::string full = "/system/bin/";
        full += binary;
        if (!TWFunc::Path_Exists(full)) {
            gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")(error_name));
            return false;
        }
        if (!UnMount(true))
            return false;
        gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(Display_Name)(binary));
        Find_Actual_Block_Device();
        std::string command = full;
        if (*flags) {
            command += " ";
            command += flags;
        }
        command += " ";
        command += Actual_Block_Device;
        LOGINFO("Repair command: %s\n", command.c_str());
        if (TWFunc::Exec_Cmd(command) == 0) {
            gui_msg("done=Done.");
            return true;
        }
        gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(Display_Name));
        return false;
    };

    if (Current_File_System == "vfat")
        return do_repair("fsck.fat", "-y", "fsck.fat");
    if (Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4")
        return do_repair("e2fsck", "-fp", "e2fsck");
    if (Current_File_System == "exfat")
        return do_repair("fsck.exfat", "", "fsck.exfat");
    if (Current_File_System == "f2fs" || Current_File_System == "mifs")
        return do_repair("fsck.f2fs", "-a", "fsck.f2fs");
    if (Current_File_System == "ntfs" || Current_File_System == "tntfs") {
        std::string Ntfsfix_Binary;
        if (TWFunc::Path_Exists("/system/bin/ntfsfix"))
            Ntfsfix_Binary = "ntfsfix";
        else if (TWFunc::Path_Exists("/system/bin/fsck.ntfs"))
            Ntfsfix_Binary = "fsck.ntfs";
        else {
            gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("ntfsfix"));
            return false;
        }
        return do_repair(Ntfsfix_Binary.c_str(), "", "ntfsfix");
    }
    return false;
}

bool TWPartition::Can_Resize() {
    if (Mount_Read_Only)
        return false;
    if ((Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") &&
        TWFunc::Path_Exists("/system/bin/resize2fs"))
        return true;
    return false;
}

bool TWPartition::Resize() {
    std::string command;

    if (Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") {
        if (!Can_Repair()) {
            LOGINFO("Cannot resize %s because %s cannot be repaired before resizing.\n", Display_Name.c_str(),
                    Display_Name.c_str());
            gui_msg(Msg(msg::kError, "cannot_resize=Cannot resize {1}.")(Display_Name));
            return false;
        }
        if (!TWFunc::Path_Exists("/system/bin/resize2fs")) {
            LOGINFO("resize2fs does not exist! Cannot resize!\n");
            gui_msg(Msg(msg::kError, "cannot_resize=Cannot resize {1}.")(Display_Name));
            return false;
        }
        // Repair will unmount so no need to do it twice
        gui_msg(Msg("repair_resize=Repairing {1} before resizing.")(Display_Name));
        if (!Repair())
            return false;
        gui_msg(Msg("resizing=Resizing {1} using {2}...")(Display_Name)("resize2fs"));
        Find_Actual_Block_Device();
        command = "/system/bin/resize2fs " + Actual_Block_Device;
        if (Length != 0) {
            unsigned long long Actual_Size = IOCTL_Get_Block_Size();
            if (Actual_Size == 0)
                return false;

            unsigned long long Block_Count;
            if (Length < 0) {
                // Reduce overall size by this length
                Block_Count = (Actual_Size / 1024LLU) - ((unsigned long long) (Length * -1) / 1024LLU);
            } else {
                // This is the size, not a size reduction
                Block_Count = ((unsigned long long) (Length) / 1024LLU);
            }
            command += " " + std::to_string(Block_Count) + "K";
        }
        LOGINFO("Resize command: %s\n", command.c_str());
        if (TWFunc::Exec_Cmd(command) == 0) {
            Update_Size(true);
            gui_msg("done=Done.");
            return true;
        } else {
            Update_Size(true);
            gui_msg(Msg(msg::kError, "unable_resize=Unable to resize {1}.")(Display_Name));
            return false;
        }
    }
    return false;
}

bool TWPartition::Backup(PartitionSettings *part_settings, pid_t *tar_fork_pid) {
    switch (Backup_Method) {
        case BackupMethod::BM_FILES:
            return Backup_Tar(part_settings, tar_fork_pid);
        case BackupMethod::BM_DD:
            return Backup_Image(part_settings);
        default:
            LOGERR("Unknown backup method for '%s'\n", Mount_Point.c_str());
            return false;
    }
}

bool TWPartition::Restore(PartitionSettings *part_settings) {
    TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, gui_parse_text("{@restoring_hdr}"));
    LOGINFO("Restore filename is: %s/%s\n", part_settings->Backup_Folder.c_str(), Backup_FileName.c_str());

    std::string Restore_File_System = Get_Restore_File_System(part_settings);

    if (Is_File_System(Restore_File_System))
        return Restore_Tar(part_settings);
    if (Is_Image(Restore_File_System))
        return Restore_Image(part_settings);

    LOGERR("Unknown restore method for '%s'\n", Mount_Point.c_str());
    return false;
}

std::string TWPartition::Get_Restore_File_System(PartitionSettings *part_settings) {
    size_t first_period, second_period;
    std::string Restore_File_System;

    // Parse backup filename to extract the file system before wiping
    first_period = Backup_FileName.find(".");
    if (first_period == std::string::npos) {
        LOGERR("Unable to find file system (first period).\n");
        return std::string();
    }
    Restore_File_System = Backup_FileName.substr(first_period + 1, Backup_FileName.size() - first_period - 1);
    second_period = Restore_File_System.find(".");
    if (second_period == std::string::npos) {
        LOGERR("Unable to find file system (second period).\n");
        return std::string();
    }
    Restore_File_System.resize(second_period);
    LOGINFO("Restore file system is: '%s'.\n", Restore_File_System.c_str());
    return Restore_File_System;
}

std::string TWPartition::Backup_Method_By_Name() {
    switch (Backup_Method) {
        case BackupMethod::BM_NONE:
            return "none";
        case BackupMethod::BM_FILES:
            return "files";
        case BackupMethod::BM_DD:
            return "dd";
        default:
            return "undefined";
    }
}

bool TWPartition::Decrypt(std::string Password) {
    LOGINFO("STUB TWPartition::Decrypt, password: '%s'\n", Password.c_str());
    // Is this needed?
    return 1;
}

bool TWPartition::Wipe_Encryption() {
    bool Save_Data_Media = Has_Data_Media;
    bool ret = false;
#ifdef TW_USE_DMCTL
    const char *userdata_mapper = "/dev/block/mapper/userdata";
#endif
    std::unique_ptr<BasePartition> base_partition(make_partition());

    if (!base_partition->PreWipeEncryption())
        return ret;

    Find_Actual_Block_Device();
    if (!Is_Present) {
        LOGINFO("Block device not present, cannot format %s.\n", Display_Name.c_str());
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }

#ifdef TW_INCLUDE_CRYPTO
    if (!UnMount(true)) {
        LOGINFO("Force unmount /data.\n");
        TWFunc::killForUseTargetProcess(Symlink_Mount_Point);
        if (!Symlink_Mount_Point.empty()) umount2(Symlink_Mount_Point.c_str(), MNT_FORCE);
        TWFunc::killForUseTargetProcess(Mount_Point);
        UnMount(false);
        if (Is_Mounted()) return false;
    }
#ifdef TW_USE_DMCTL
    if (Mount_Point == "/data" && TWFunc::Path_Exists(userdata_mapper)) {
        LOGINFO("Removing metadata-encryption userdata mapping before format.\n");
        int dmctl_result = TWFunc::Exec_Cmd("dmctl delete userdata", false);
        if (dmctl_result != 0 && TWFunc::Path_Exists(userdata_mapper)) {
            LOGERR("Unable to remove metadata-encryption userdata mapping.\n");
            return false;
        }
        for (int retry = 0;
             retry < 100 && TWFunc::Path_Exists(userdata_mapper);
             retry++)
            usleep(10000);
        if (TWFunc::Path_Exists(userdata_mapper)) {
            LOGERR("Metadata-encryption userdata mapping did not disappear before format.\n");
            return false;
        }
    }
#endif
    // if (Is_Decrypted && !Decrypted_Block_Device.empty()) {
    //		if (delete_crypto_blk_dev((char*)("userdata")) != 0) {
    //			LOGERR("Error deleting crypto block device, continuing anyway.\n");
    //		}
    // }
#endif
    Has_Data_Media = false;
    Decrypted_Block_Device = "";
    Is_Decrypted = false;
    Is_Encrypted = false;
    if (Wipe(Fstab_File_System)) {
        Has_Data_Media = Save_Data_Media;
        DataManager::SetValue(TW_IS_ENCRYPTED, 0);
        gui_msg("format_data_msg=You may need to reboot recovery to be able to use /data again.");
        if (Is_FBE) {
            gui_msg(Msg(msg::kWarning,
                        "data_media_fbe_msg=TWRP will not recreate /data/media on an FBE device. Please reboot into your rom to create /data/media."));
        } else {
            if (Has_Data_Media && !Symlink_Mount_Point.empty()) {
                if (Mount(false))
                    PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
            }
        }

        ret = true;
        if (!Key_Directory.empty())
            ret = PartitionManager.Wipe_By_Path(Key_Directory);
        if (ret)
            ret = base_partition->PostWipeEncryption();
        return ret;
    }
    Has_Data_Media = Save_Data_Media;
    gui_err("format_data_err=Unable to format to remove encryption.");
    if (Has_Data_Media && Mount(false))
        PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
    return ret;
}

void TWPartition::Check_FS_Type() {
    const char *type;

    // Skip probing when explicitly disabled for this partition
    if (Ignore_Blkid) return;

    Find_Actual_Block_Device();
    if (!Is_Present) return;

    blkid_probe pr = blkid_new_probe_from_filename(Actual_Block_Device.c_str());
    // /storage probes a directory, so NULL is routine here, and
    // blkid_do_fullprobe() does not check what it is handed.
    if (!pr) {
        LOGINFO("Can't probe device %s\n", Actual_Block_Device.c_str());
        return;
    }
    // Free the probe on every exit path from here (fullprobe fail, lookup fail, success).
    auto pr_guard = android::base::make_scope_guard([&] { blkid_free_probe(pr); });
    if (blkid_do_fullprobe(pr)) {
        LOGINFO("Can't probe device %s\n", Actual_Block_Device.c_str());
        return;
    }

    if (blkid_probe_lookup_value(pr, "TYPE", &type, nullptr) < 0) {
        LOGINFO("can't find filesystem on device %s\n", Actual_Block_Device.c_str());
        return;
    }

    Current_File_System = type;
    if (fs_flags.size() > 1) {
        auto found = std::ranges::find(fs_flags, Current_File_System, &partition_fs_flags_struct::File_System);
        // If we don't find a match, we default the flags to the first set of flags that we received from the fstab
        if (found == fs_flags.end())
            found = fs_flags.begin();
        if (Mount_Flags != found->Mount_Flags || Mount_Options != found->Mount_Options) {
            Mount_Flags = found->Mount_Flags;
            Mount_Options = found->Mount_Options;
            LOGINFO("Mount_Flags: %i, Mount_Options: %s\n", Mount_Flags, Mount_Options.c_str());
        }
    }
}

bool TWPartition::Wipe_EXTFS(std::string File_System) {
    if (!UnMount(true))
        return false;

    if (!TWFunc::Path_Exists("/system/bin/mke2fs") || !TWFunc::Path_Exists("/system/bin/e2fsdroid"))
        return Wipe_RMRF();

    int ret;
    bool NeedPreserveFooter = true;

    Find_Actual_Block_Device();
    if (!Is_Present) {
        LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }

    /**
     * On decrypted devices, IOCTL_Get_Block_Size calculates size on device mapper,
     * so there's no need to preserve footer.
     */
    if ((Is_Decrypted && !Decrypted_Block_Device.empty()) ||
        Crypto_Key_Location != "footer") {
        NeedPreserveFooter = false;
    }

    unsigned long long dev_sz = TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
    if (!dev_sz)
        return false;

    if (NeedPreserveFooter)
        Length < 0 ? dev_sz += Length : dev_sz -= CRYPT_FOOTER_OFFSET;

    std::string size_str = std::to_string(dev_sz / 4096);
    std::string cmd;

    gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mke2fs"));

    // Execute mke2fs to create empty ext4 filesystem
    cmd = "mke2fs -t " + File_System + " -b 4096 -I 512";
    if (Needs_Metadata_Csum) {
        cmd += " -O metadata_csum,64bit,extent";
    }
    cmd += " " + Actual_Block_Device + " " + size_str;
    LOGINFO("mke2fs command: %s\n", cmd.c_str());
    ret = TWFunc::Exec_Cmd(cmd);
    if (ret) {
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }

    if (TWFunc::Path_Exists("/system/bin/e2fsdroid")) {
        const std::string &File_Contexts_Entry = (Mount_Point == "/system_root" ? "/" : Mount_Point);
        char *secontext = nullptr;
        if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, File_Contexts_Entry.c_str(), S_IFDIR) < 0) {
            LOGINFO("Cannot lookup security context for '%s'\n", Mount_Point.c_str());
        } else {
            // Execute e2fsdroid to initialize selinux context
            if (Mount_Point == TW_PERSIST_ROOT) {
                Mount(true);
                TWFunc::removeDir(TW_PERSIST_ROOT "/lost+found", false);
                UnMount(true);
            }
            std::string cmd = "e2fsdroid -e -S /file_contexts -a " + File_Contexts_Entry + " " + Actual_Block_Device;
            LOGINFO("e2fsdroid command: %s\n", cmd.c_str());
            ret = TWFunc::Exec_Cmd(cmd);
            if (ret) {
                gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
                return false;
            }
        }
    } else {
        LOGINFO("e2fsdroid not present\n");
    }

    if (NeedPreserveFooter)
        Wipe_Crypto_Key();
    Current_File_System = File_System;
    Recreate_AndSec_Folder();
    gui_msg("done=Done.");
    return true;
}

bool TWPartition::Wipe_EXT4() {
#ifdef USE_EXT4
    int ret;
    bool NeedPreserveFooter = true;

    if (!UnMount(true))
        return false;

    Find_Actual_Block_Device();
    if (!Is_Present) {
        LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }

    /**
     * On decrypted devices, IOCTL_Get_Block_Size calculates size on device mapper,
     * so there's no need to preserve footer.
     */
    if ((Is_Decrypted && !Decrypted_Block_Device.empty()) ||
        Crypto_Key_Location != "footer") {
        NeedPreserveFooter = false;
    }

    unsigned long long dev_sz = TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
    if (!dev_sz)
        return false;

    if (NeedPreserveFooter)
        Length < 0 ? dev_sz += Length : dev_sz -= CRYPT_FOOTER_OFFSET;

    char *secontext = nullptr;

    gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("make_ext4fs"));

    if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, Mount_Point.c_str(), S_IFDIR) < 0) {
        LOGINFO("Cannot lookup security context for '%s'\n", Mount_Point.c_str());
        ret = make_ext4fs(Actual_Block_Device.c_str(), dev_sz, Mount_Point.c_str(), nullptr);
    } else {
        ret = make_ext4fs(Actual_Block_Device.c_str(), dev_sz, Mount_Point.c_str(), selinux_handle);
    }
    if (ret != 0) {
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    } else {
        if (NeedPreserveFooter)
            Wipe_Crypto_Key();
        std::string sedir = Mount_Point + "/lost+found";
        PartitionManager.Mount_By_Path(sedir.c_str(), true);
        rmdir(sedir.c_str());
        mkdir(sedir.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
        return true;
    }
#else
    return Wipe_EXTFS("ext4");
#endif
}

bool TWPartition::Wipe_FAT() {
    if (!UnMount(true)) return false;

    if (TWFunc::Path_Exists("/system/bin/mkfs.fat")) {
        gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mkfs.fat"));
        Find_Actual_Block_Device();
        std::string cmd = "mkfs.fat " + Actual_Block_Device;
        if (TWFunc::Exec_Cmd(cmd) == 0) {
            Current_File_System = "vfat";
            Recreate_AndSec_Folder();
            gui_msg("done=Done.");
            return true;
        } else {
            gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
            return false;
        }
        return true;
    } else
        return Wipe_RMRF();

    return false;
}

bool TWPartition::Wipe_EXFAT() {
    if (!UnMount(true)) return false;

    if (TWFunc::Path_Exists("/system/bin/mkfs.exfat")) {
        gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mkfs.exfat"));
        Find_Actual_Block_Device();
        std::string cmd = "mkfs.exfat " + Actual_Block_Device;
        if (TWFunc::Exec_Cmd(cmd) == 0) {
            Recreate_AndSec_Folder();
            gui_msg("done=Done.");
            return true;
        } else {
            gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
            return false;
        }
        return true;
    }
    return false;
}

bool TWPartition::Wipe_RMRF() {
    if (!Mount(true)) return false;

    // This is the only wipe that leaves the partition mounted, so we
    // must manually remove the partition from MTP if it is a storage
    // partition.
    if (Is_Storage)
        PartitionManager.Remove_MTP_Storage(MTP_Storage_ID);

    gui_msg(Msg("remove_all=Removing all files under '{1}'")(Mount_Point));
    TWFunc::removeDir(Mount_Point, true);
    Recreate_AndSec_Folder();
    return true;
}

bool TWPartition::Wipe_F2FS() {
    std::string f2fs_command;

    if (!UnMount(true))
        return false;

    if (TWFunc::Path_Exists("/system/bin/make_f2fs"))
        f2fs_command = "/system/bin/make_f2fs -g android";
    else {
        LOGINFO("make_f2fs binary not found, using rm -rf to wipe.\n");
        return Wipe_RMRF();
    }

    bool NeedPreserveFooter = true;
    bool needs_casefold = false;

    Find_Actual_Block_Device();
    if (!Is_Present) {
        LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }

    if (Mount_Point == "/data") {
        needs_casefold = android::base::GetBoolProperty("external_storage.casefold.enabled", false);
    }

    unsigned long long dev_sz = TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
    if (!dev_sz)
        return false;

    if (NeedPreserveFooter)
        Length < 0 ? dev_sz += Length : dev_sz -= CRYPT_FOOTER_OFFSET;

    // Project ID
    f2fs_command += " -O project_quota,extra_attr";

    if (needs_casefold)
        f2fs_command += " -O casefold -C utf8";

    if (Needs_Fs_Compress)
        f2fs_command += " -O compression,extra_attr";

    f2fs_command += " " + Actual_Block_Device + " " + std::to_string(dev_sz / 4096);

    if (TWFunc::Path_Exists("/system/bin/sload_f2fs")) {
        f2fs_command += " && sload_f2fs -t /data " + Actual_Block_Device;
    }

    /**
     * On decrypted devices, IOCTL_Get_Block_Size calculates size on device mapper,
     * so there's no need to preserve footer.
     */
    if ((Is_Decrypted && !Decrypted_Block_Device.empty()) ||
        Crypto_Key_Location != "footer") {
        NeedPreserveFooter = false;
    }
    LOGINFO("make_f2fs command: %s\n", f2fs_command.c_str());

    if (TWFunc::Exec_Cmd(f2fs_command) == 0) {
        if (NeedPreserveFooter)
            Wipe_Crypto_Key();
        Recreate_AndSec_Folder();
        gui_msg("done=Done.");
        return true;
    } else {
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }
    return true;
}

bool TWPartition::Wipe_NTFS() {
    std::string cmd;
    std::string Ntfsmake_Binary;

    if (!UnMount(true))
        return false;

    if (TWFunc::Path_Exists("/system/bin/mkntfs"))
        Ntfsmake_Binary = "mkntfs";
    else if (TWFunc::Path_Exists("/system/bin/mkfs.ntfs"))
        Ntfsmake_Binary = "mkfs.ntfs";
    else
        return false;

    gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)(Ntfsmake_Binary));
    Find_Actual_Block_Device();
    cmd = "/system/bin/" + Ntfsmake_Binary + " " + Actual_Block_Device;
    if (TWFunc::Exec_Cmd(cmd) == 0) {
        Recreate_AndSec_Folder();
        gui_msg("done=Done.");
        return true;
    } else {
        gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
        return false;
    }
    return false;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
    bool ret = false;

    if (!Mount(true))
        return false;

    gui_msg("wiping_data=Wiping data without wiping /data/media ...");
    ret = Wipe_Data_Without_Wiping_Media_Func(Mount_Point + "/");
    if (ret)
        gui_msg("done=Done.");
    return ret;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media_Func(const std::string & parent __unused) {
    std::string dir;

    DIR *d = opendir(parent.c_str());
    if (d) {
        // Close the directory on every exit path (recursion failure or normal return).
        auto d_guard = android::base::make_scope_guard([&] { closedir(d); });
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

            dir = parent;
            dir.append(de->d_name);
            if (wipe_exclusions.check_skip_dirs(dir)) {
                LOGINFO("skipped '%s'\n", dir.c_str());
                continue;
            }
            if (de->d_type == DT_DIR) {
                dir.append("/");
                if (!Wipe_Data_Without_Wiping_Media_Func(dir)) {
                    return false;
                }
#ifdef TW_WORKAROUND_BACKUP_BUG
                if (dir == "/data/data/")
                    // temporary workaround for error 255 when restoring data backups in 16 branch builds
                    LOGINFO("DEBUG: TWRP: skipped /data/data/\n");
                else
#endif
                rmdir(dir.c_str());
            } else if (de->d_type == DT_REG || de->d_type == DT_LNK || de->d_type == DT_FIFO || de->d_type == DT_SOCK) {
                if (unlink(dir.c_str()) != 0)
                    LOGINFO("Unable to unlink '%s': %s\n", dir.c_str(), strerror(errno));
            }
        }
        return true;
    }
    gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Mount_Point)(strerror(errno)));
    return false;
}

void TWPartition::Wipe_Crypto_Key() {
    Find_Actual_Block_Device();
    if (Crypto_Key_Location.empty())
        return;
    else if (Crypto_Key_Location == "footer") {
        int fd = open(Actual_Block_Device.c_str(), O_RDWR);
        if (fd < 0) {
            gui_print_color("warning", "Unable to open '%s' to wipe crypto key\n", Actual_Block_Device.c_str());
            return;
        }

        unsigned int block_count;
        if ((ioctl(fd, BLKGETSIZE, &block_count)) == -1) {
            gui_print_color("warning", "Unable to get block size for wiping crypto footer.\n");
        } else {
            int newlen = Length < 0 ? -Length : CRYPT_FOOTER_OFFSET;
            off64_t offset = (static_cast<off64_t>(block_count) * 512) - newlen;
            if (lseek64(fd, offset, SEEK_SET) == -1) {
                gui_print_color("warning", "Unable to lseek64 for wiping crypto footer.\n");
            } else {
                std::unique_ptr<char[]> buffer(new(std::nothrow) char[newlen]());
                if (!buffer) {
                    gui_print_color("warning", "Failed to malloc for wiping crypto footer.\n");
                } else {
                    int ret = write(fd, buffer.get(), newlen);
                    if (ret != newlen) {
                        gui_print_color("warning", "Failed to wipe crypto footer.\n");
                    } else {
                        LOGINFO("Successfully wiped crypto footer.\n");
                    }
                }
            }
        }
        close(fd);
    } else {
        if (TWFunc::IOCTL_Get_Block_Size(Crypto_Key_Location.c_str()) >= 16384LLU) {
            std::string Command = "dd of='" + Crypto_Key_Location + "' if=/dev/zero bs=16384 count=1";
            TWFunc::Exec_Cmd(Command);
        } else {
            LOGINFO("Crypto key location reports size < 16K so not wiping crypto footer.\n");
        }
    }
}

bool TWPartition::Backup_Tar(PartitionSettings *part_settings, pid_t *tar_fork_pid) {
    std::string Full_FileName;
    twrpTar tar;

    if (!Mount(true))
        return false;

    TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Backup_Display_Name, gui_parse_text("{@backing}"));
    gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

    DataManager::GetValue(TW_USE_COMPRESSION_VAR, tar.use_compression);

    if (Can_Encrypt_Backup) {
        DataManager::GetValue("tw_encrypt_backup", tar.use_encryption);
        if (tar.use_encryption) {
            if (Use_Userdata_Encryption)
                tar.userdata_encryption = tar.use_encryption;
            std::string Password;
            DataManager::GetValue("tw_backup_password", Password);
            tar.setpassword(Password);
        } else {
            tar.use_encryption = 0;
        }
    }

    Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
    Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;
    if (Has_Data_Media)
#ifdef TW_WORKAROUND_BACKUP_BUG
    gui_msg(Msg(msg::kWarning,
                "backup_storage_warning=Backups of {1} do not include any files in internal storage such as pictures or downloads.")(
        Display_Name));
#else
        LOGERR("Backups of %s are currently BROKEN in the 16 branch. There is little point in making backups of %s\n\n",
           Display_Name.c_str(), Display_Name.c_str());
#endif

    if (Mount_Point == "/data" && DataManager::GetIntValue(TW_IS_FBE)) {
        std::vector<users_struct> *userList = PartitionManager.Get_Users_List();
        for (const users_struct &user: *userList) {
            if (!user.isDecrypted && user.userId != "0") {
                gui_msg(Msg(msg::kWarning,
                            "backup_storage_undecrypt_warning=Backup will not include some files from user {1} "
                            "because the user is not decrypted.")(user.userId));
                backup_exclusions.add_absolute_dir("/data/system_ce/" + user.userId);
                backup_exclusions.add_absolute_dir("/data/misc_ce/" + user.userId);
                backup_exclusions.add_absolute_dir("/data/vendor_ce/" + user.userId);
                backup_exclusions.add_absolute_dir("/data/media/" + user.userId);
                backup_exclusions.add_absolute_dir("/data/user/" + user.userId);
            }
        }
    }
    tar.part_settings = part_settings;
    tar.backup_exclusions = &backup_exclusions;
    tar.setdir(Backup_Path);
    tar.setfn(Full_FileName);
    tar.setsize(Backup_Size);
    tar.partition_name = Backup_Name;
    tar.backup_folder = part_settings->Backup_Folder;
    if (tar.createTarFork(tar_fork_pid) != 0)
        return false;
    return true;
}

bool TWPartition::Backup_Image(PartitionSettings *part_settings) {
    std::string Full_FileName, adb_file_name;

    TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
    gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

    Backup_FileName = Backup_Name + "." + Current_File_System + ".win";

    if (part_settings->adbbackup) {
        Full_FileName = TW_ADB_BACKUP;
        adb_file_name = part_settings->Backup_Folder + "/" + Backup_FileName;
    } else
        Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;

    part_settings->total_restore_size = Backup_Size;

    if (part_settings->adbbackup) {
        if (!twadbbu::Write_TWIMG(adb_file_name, Backup_Size))
            return false;
    }

    if (!Raw_Read_Write(part_settings))
        return false;

    if (part_settings->adbbackup) {
        if (!twadbbu::Write_TWEOF())
            return false;
    }
    return true;
}

bool TWPartition::Raw_Read_Write(PartitionSettings *part_settings) {
    unsigned long long RW_Block_Size, Remain = Backup_Size;
    ssize_t bs;
    unsigned long long backedup_size = 0;
    std::string srcfn, destfn;

    if (part_settings->PM_Method == PartitionManagerOp::PM_BACKUP) {
        srcfn = Actual_Block_Device;
        if (part_settings->adbbackup)
            destfn = TW_ADB_BACKUP;
        else {
            destfn = part_settings->Backup_Folder + "/" + Backup_FileName;
        }
    } else {
#ifdef TW_ENABLE_BLKDISCARD
        BlkDiscard();
#endif
        destfn = Actual_Block_Device;
        if (part_settings->adbbackup) {
            srcfn = TW_ADB_RESTORE;
        } else {
            srcfn = part_settings->Backup_Folder + "/" + Backup_FileName;
            Remain = TWFunc::Get_File_Size(srcfn);
        }
    }

    android::base::unique_fd src_fd(open(srcfn.c_str(), O_RDONLY | O_LARGEFILE));
    if (src_fd.get() < 0) {
        gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(srcfn.c_str())(strerror(errno)));
        return false;
    }

    android::base::unique_fd dest_fd(
        open(destfn.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, S_IRUSR | S_IWUSR));
    if (dest_fd.get() < 0) {
        gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(destfn.c_str())(strerror(errno)));
        return false;
    }

    LOGINFO("Reading '%s', writing '%s'\n", srcfn.c_str(), destfn.c_str());

    if (part_settings->adbbackup) {
        RW_Block_Size = MAX_ADB_READ;
        bs = MAX_ADB_READ;
    } else {
        RW_Block_Size = kMiB; // 1MB
        bs = static_cast<ssize_t>(RW_Block_Size);
    }

    std::unique_ptr<char[]> buffer(new(std::nothrow) char[static_cast<size_t>(bs)]());
    if (!buffer) {
        LOGINFO("Raw_Read_Write failed to malloc\n");
        return false;
    }

    if (part_settings->progress)
        part_settings->progress->SetPartitionSize(part_settings->total_restore_size);

    while (Remain > 0) {
        if (Remain < RW_Block_Size)
            bs = static_cast<ssize_t>(Remain);
        if (read(src_fd.get(), buffer.get(), bs) != bs) {
            LOGINFO("Error reading source fd (%s)\n", strerror(errno));
            return false;
        }
        if (write(dest_fd.get(), buffer.get(), bs) != bs) {
            LOGINFO("Error writing destination fd (%s)\n", strerror(errno));
            return false;
        }
        backedup_size += (unsigned long long) (bs);
        Remain -= (unsigned long long) (bs);
        if (part_settings->progress)
            part_settings->progress->UpdateSize(backedup_size);
        if (PartitionManager.Check_Backup_Cancel() != 0)
            return false;
    }
    if (part_settings->progress)
        part_settings->progress->UpdateDisplayDetails(true);
    fsync(dest_fd.get());

    if (!part_settings->adbbackup && part_settings->PM_Method == PartitionManagerOp::PM_BACKUP) {
        tw_set_default_metadata(destfn.c_str());
        LOGINFO("Restored default metadata for %s\n", destfn.c_str());
    }

    return true;
}

unsigned long long TWPartition::Get_Restore_Size(PartitionSettings *part_settings) {
    if (!part_settings->adbbackup) {
        InfoManager restore_info(part_settings->Backup_Folder + "/" + Backup_Name + ".info");
        if (restore_info.LoadValues() == 0) {
            if (restore_info.GetValue("backup_size", Restore_Size) == 0) {
                LOGINFO("Read info file, restore size is %llu\n", Restore_Size);
                return Restore_Size;
            }
        }
    }

    std::string Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;
    std::string Restore_File_System = Get_Restore_File_System(part_settings);

    if (Is_Image(Restore_File_System)) {
        Restore_Size = TWFunc::Get_File_Size(Full_FileName);
        return Restore_Size;
    }

    twrpTar tar;
    tar.setdir(Backup_Path);
    tar.setfn(Full_FileName);
    tar.backup_name = Full_FileName;
    std::string Password;
    DataManager::GetValue("tw_restore_password", Password);
    if (!Password.empty())
        tar.setpassword(Password);
    tar.partition_name = Backup_Name;
    tar.backup_folder = part_settings->Backup_Folder;
    tar.part_settings = part_settings;
    Restore_Size = tar.get_size();
    return Restore_Size;
}

bool TWPartition::Restore_Tar(PartitionSettings *part_settings) {
    std::string Full_FileName;
    bool ret = false;
    std::string Restore_File_System = Get_Restore_File_System(part_settings);

    if (Has_Android_Secure) {
        if (!Wipe_AndSec())
            return false;
    } else {
        gui_msg(Msg("wiping=Wiping {1}")(Backup_Display_Name));
        if (Has_Data_Media && Mount_Point == "/data" && Restore_File_System != Current_File_System) {
            gui_msg(Msg(msg::kWarning,
                        "datamedia_fs_restore=WARNING: This /data backup was made with {1} file system! The backup may not boot unless you change back to {1}.")(
                Restore_File_System));
            if (!Wipe_Data_Without_Wiping_Media())
                return false;
        } else {
            if (!Wipe(Restore_File_System))
                return false;
        }
    }
    TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Backup_Display_Name, gui_parse_text("{@restoring_hdr}"));
    gui_msg(Msg("restoring=Restoring {1}...")(Backup_Display_Name));

    // Remount as read/write as needed so we can restore the backup
    if (!ReMount_RW(true))
        return false;

    Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;
    twrpTar tar;
    tar.part_settings = part_settings;
    tar.setdir(Backup_Path);
    tar.setfn(Full_FileName);
    tar.backup_name = Backup_Name;
    std::string Password;
    DataManager::GetValue("tw_restore_password", Password);
    if (!Password.empty())
        tar.setpassword(Password);
    part_settings->progress->SetPartitionSize(Get_Restore_Size(part_settings));
    if (tar.extractTarFork() != 0)
        ret = false;
    else
        ret = true;
#ifdef HAVE_CAPABILITIES
    // Restore capabilities to the run-as binary
    if (Mount_Point == PartitionManager.Get_Android_Root_Path() && Mount(true) && TWFunc::Path_Exists(
            "/system/bin/run-as")) {
        struct vfs_cap_data cap_data;
        uint64_t capabilities = (1 << CAP_SETUID) | (1 << CAP_SETGID);

        memset(&cap_data, 0, sizeof(cap_data));
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
#endif
    if (Mount_Read_Only || Mount_Flags & MS_RDONLY)
        // Remount as read only when restoration is complete
        ReMount(true);

    return ret;
}

bool TWPartition::Restore_Image(PartitionSettings *part_settings) {
    std::string Full_FileName;
    std::string Restore_File_System = Get_Restore_File_System(part_settings);

    TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Backup_Display_Name, gui_parse_text("{@restoring_hdr}"));
    gui_msg(Msg("restoring=Restoring {1}...")(Backup_Display_Name));

    if (part_settings->adbbackup)
        Full_FileName = TW_ADB_RESTORE;
    else
        Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;

    if (Restore_File_System == "emmc") {
        if (!part_settings->adbbackup)
            part_settings->total_restore_size = static_cast<uint64_t>(TWFunc::Get_File_Size(Full_FileName));
        if (!Raw_Read_Write(part_settings))
            return false;
    }

    if (part_settings->adbbackup) {
        if (!twadbbu::Write_TWEOF())
            return false;
    }
    return true;
}

// Is_Decrypted and TW_IS_DECRYPTED both go true as soon as the metadata layer is
// up, so ask the user list instead. Empty when the device isn't FBE.
bool TWPartition::Data_Is_Locked() {
    for (const users_struct &user: *PartitionManager.Get_Users_List()) {
        if (user.userId == "0")
            return !user.isDecrypted;
    }
    return false;
}

struct Async_Size_State {
    std::mutex lock;
    bool running = false;
    TWPartition *pending_partition = nullptr;
    unsigned long long pending_size = 0;
};

// Keep state alive until detached workers finish.
static Async_Size_State &Get_Async_Size_State() {
    static Async_Size_State *state = new Async_Size_State;
    return *state;
}

void TWPartition::Update_Data_Size_Async() {
    Async_Size_State &state = Get_Async_Size_State();
    {
        std::lock_guard<std::mutex> lock(state.lock);
        if (!Backup_Size_Provisional || state.running)
            return;
        state.running = true;
    }

    // The worker must use a snapshot; Backup_Tar() mutates the real list.
    TWExclude exclusions = backup_exclusions;
    std::string path = Mount_Point;

    if (Is_Mounted()) {
        TWPartition *partition = this;
        std::string dev = Decrypted_Block_Device.empty() ? Actual_Block_Device : Decrypted_Block_Device;
        std::thread([partition, exclusions, path, dev]() mutable {
            unsigned long long size = 0;
            // Prefer filesystem counters, then fall back to the directory walk.
            uint64_t es = exclusions.Get_Exclusions_Folder_Size();
            if (es > 0 && !dev.empty()) {
                char cmdBuf[256];
                const char _cmd[] = "dump.f2fs -d1 %s | grep %s | awk -F ': ' '{print $2}' | awk -F ']' '{print $1}'";
                int64_t _Used = 0;
                snprintf(cmdBuf, sizeof(cmdBuf), _cmd, dev.c_str(), "valid_block_count");
                std::string result;
                if (TWFunc::Exec_Cmd(cmdBuf, result, false) == 0) {
                    uint64_t USCount = strtoull(result.c_str(), nullptr, 10);
                    if (USCount > 0 && USCount <= 354674688ULL) {
                        _Used = USCount * 4096LLU;
                        if (static_cast<int64_t>(_Used - es) > 0) {
                            snprintf(cmdBuf, sizeof(cmdBuf), _cmd, dev.c_str(), "valid_inode_count");
                            result.clear();
                            if (TWFunc::Exec_Cmd(cmdBuf, result, false) == 0) {
                                uint64_t UICount = strtoull(result.c_str(), nullptr, 10);
                                if (UICount > 0 && _Used > UICount * 4096ULL)
                                    _Used -= UICount * 4096ULL;
                            }
                            if (static_cast<int64_t>(_Used - es) > 0)
                                size = _Used - es;
                        }
                    }
                }
                LOGINFO("Data size: f2fs fast path %s (exclusions %llu bytes).\n",
                        size > 0 ? "hit" : "miss", (unsigned long long)es);
            }
            if (size == 0) {
                // dump.f2fs unavailable: use the old walk.
                LOGINFO("Data size: falling back to folder walk.\n");
                size = exclusions.Get_Folder_Size(path, false);
            }
            Async_Size_State &state = Get_Async_Size_State();
            std::lock_guard<std::mutex> lock(state.lock);
            state.pending_partition = partition;
            state.pending_size = size;
        }).detach();
    } else {
        LOGINFO("'%s' is not mounted, keeping the statfs backup size.\n", path.c_str());
        std::lock_guard<std::mutex> lock(state.lock);
        state.running = false;
    }
}

void TWPartition::Apply_Async_Data_Size() {
    Async_Size_State &state = Get_Async_Size_State();
    unsigned long long size;
    {
        std::lock_guard<std::mutex> lock(state.lock);
        if (state.pending_partition != this)
            return;
        size = state.pending_size;
        state.pending_partition = nullptr;
        state.running = false;
    }
    Used = size;
    Backup_Size = size;
    Backup_Size_Provisional = false;
    int bak = static_cast<int>(size / kMiB);
    DataManager::SetValue(TW_BACKUP_DATA_SIZE, bak);
    LOGINFO("Data backup size is %iMB.\n", bak);
}

bool TWPartition::Update_Size(bool Display_Error, bool Defer_Folder_Size) {
    bool ret = false, Was_Already_Mounted = false, ro = false;

    Find_Actual_Block_Device();

    if (Actual_Block_Device.empty())
        return false;

    ro = Mount_Read_Only;
    Mount_Read_Only = true;
    auto restore_ro = android::base::make_scope_guard([&]() { Mount_Read_Only = ro; });

    if (!Can_Be_Mounted && !Is_Encrypted) {
        if (TWFunc::Path_Exists(Actual_Block_Device) && Find_Partition_Size()) {
            Used = Size;
            Backup_Size = Size;
            return true;
        }
        return false;
    }

    Was_Already_Mounted = Is_Mounted();

    if (Removable || Is_Encrypted) {
        if (!Mount(false))
            return true;
    } else if (!Mount(Display_Error))
        return false;

    ret = Get_Size_Via_statfs(Display_Error);
    if (!ret || Size == 0) {
        if (!Get_Size_Via_df(Display_Error)) {
            if (!Was_Already_Mounted)
                UnMount(false);
            return false;
        }
    }

    if (Has_Data_Media) {
        if (Mount(Display_Error)) {
            if (Defer_Folder_Size) {
                // Leave the statfs figure in place for now. Whoever deferred
                // decides whether the walk is worth starting.
                Backup_Size_Provisional = true;
                LOGINFO("Deferring the data backup size to a background walk.\n");
            } else {
                Used = backup_exclusions.Get_Folder_Size(Mount_Point);
                Backup_Size = Used;
                Backup_Size_Provisional = false;
                int bak = static_cast<int>(Used / kMiB);
                int fre = static_cast<int>(Free / kMiB);
                LOGINFO("Data backup size is %iMB, free: %iMB.\n", bak, fre);
            }
        } else {
            if (!Was_Already_Mounted)
                UnMount(false);
            return false;
        }
    } else if (Has_Android_Secure) {
        if (Mount(Display_Error))
            Backup_Size = backup_exclusions.Get_Folder_Size(Backup_Path);
        else {
            if (!Was_Already_Mounted)
                UnMount(false);
            return false;
        }
    }
    if (!Was_Already_Mounted)
        UnMount(false);
    return true;
}

// Whatever the volume calls itself, empty when it has no label.
static std::string Get_Volume_Label(const std::string &Block_Device) {
    const char *label;
    std::string name;

    blkid_probe pr = blkid_new_probe_from_filename(Block_Device.c_str());
    if (!pr)
        return name;
    // Free the probe on every exit path from here.
    auto pr_guard = android::base::make_scope_guard([&] { blkid_free_probe(pr); });
    if (blkid_do_fullprobe(pr) == 0 && blkid_probe_lookup_value(pr, "LABEL", &label, nullptr) == 0)
        name = label;
    return name;
}

bool TWPartition::Find_Wildcard_Block_Devices(const std::string &Device) {
    // we will need to create separate mount points for each partition found and we use this index to name each one
    int mount_point_index = 0;
    std::string Path = TWFunc::Get_Path(Device);
    std::string Dev = TWFunc::Get_Filename(Device);
    size_t wildcard_index = Dev.find("*");
    if (wildcard_index != std::string::npos)
        Dev = Dev.substr(0, wildcard_index);
    wildcard_index = Dev.size();
    DIR *d = opendir(Path.c_str());
    if (!d) {
        LOGINFO("Error opening '%s': %s\n", Path.c_str(), strerror(errno));
        return false;
    }
    struct dirent *de;
    while ((de = readdir(d))) {
        std::string_view name(de->d_name);
        if (de->d_type != DT_BLK || name.size() <= wildcard_index || name.substr(0, wildcard_index) != Dev)
            continue;

        // Get_Path() keeps the trailing slash, so do not add another one.
        std::string item = Path + de->d_name;
        if (PartitionManager.Find_Partition_By_Block_Device(item))
            continue;
        TWPartition *part = new TWPartition;
        std::string buffer = std::format("{} {}-{} auto defaults defaults", item, Mount_Point, ++mount_point_index);
        part->Process_Fstab_Line(buffer.c_str(), false, nullptr);
        // Prefer the label, number whatever name is already taken.
        std::string display = Get_Volume_Label(item);
        if (display.empty())
            display = Storage_Name;
        if (PartitionManager.Storage_Name_In_Use(display))
            display += " " + TWFunc::to_string(mount_point_index);
        part->Storage_Name = display;
        part->Display_Name = display;
        part->Primary_Block_Device = item;
        part->Wildcard_Block_Device = false;
        part->Is_SubPartition = true;
        part->SubPartition_Of = Mount_Point;
        part->Is_Storage = Is_Storage;
        part->Can_Be_Mounted = true;
        part->Removable = true;
        part->Can_Be_Wiped = Can_Be_Wiped;
        part->Wipe_Available_in_GUI = Wipe_Available_in_GUI;
        part->Find_Actual_Block_Device();
        part->Update_Size(false);
        Has_SubPartition = true;
        PartitionManager.Output_Partition(part);
        PartitionManager.Add_Partition(part);
    }
    closedir(d);
    return (mount_point_index > 0);
}

void TWPartition::Find_Actual_Block_Device() {
    if (!Sysfs_Entry.empty() && Primary_Block_Device.empty() && Decrypted_Block_Device.empty()) {
        /* Sysfs_Entry.empty() indicates if this is a sysfs entry that begins with /device/
         * If we have a syfs entry then we are looking for this device from a uevent add.
         * The uevent add will set the primary block device based on the data we receive from
         * after checking for adopted storage. If the device ends up being adopted, then the
         * decrypted block device will be set instead of the primary block device. */
        Is_Present = false;
        return;
    }
    if (Wildcard_Block_Device && !Is_Adopted_Storage) {
        Is_Present = false;
        Actual_Block_Device = "";
        Can_Be_Mounted = false;
        if (!Find_Wildcard_Block_Devices(Primary_Block_Device)) {
            std::string Dev = Primary_Block_Device.substr(0, Primary_Block_Device.find("*"));
            if (TWFunc::Path_Exists(Dev)) {
                Is_Present = true;
                Can_Be_Mounted = true;
                Actual_Block_Device = Dev;
            }
        }
        return;
    } else if (Is_Decrypted && !Decrypted_Block_Device.empty()) {
        Actual_Block_Device = Decrypted_Block_Device;
        if (TWFunc::Path_Exists(Decrypted_Block_Device)) {
            Is_Present = true;
            return;
        }
    } else if (SlotSelect && TWFunc::Path_Exists(Primary_Block_Device + PartitionManager.Get_Active_Slot_Suffix())) {
        Actual_Block_Device = Primary_Block_Device + PartitionManager.Get_Active_Slot_Suffix();
        unlink(Primary_Block_Device.c_str());
        symlink(Actual_Block_Device.c_str(), Primary_Block_Device.c_str());
        // we create a non-slot symlink pointing to the currently selected slot which may assist zips with installing
        Is_Present = true;
        return;
    } else if (TWFunc::Path_Exists(Primary_Block_Device)) {
        Is_Present = true;
        Actual_Block_Device = Primary_Block_Device;
        return;
    }
    if (!Alternate_Block_Device.empty() && TWFunc::Path_Exists(Alternate_Block_Device)) {
        Actual_Block_Device = Alternate_Block_Device;
        Is_Present = true;
    } else {
        Is_Present = false;
    }
}

void TWPartition::Recreate_Media_Folder() {
    std::string Command;
    std::string Media_Path = Mount_Point + "/media";

    if (Is_FBE) {
        LOGINFO("Not recreating media folder on FBE\n");
        return;
    }
    if (!Mount(true)) {
        gui_msg(Msg(msg::kError, "recreate_folder_err=Unable to recreate {1} folder.")(Media_Path));
    } else if (!TWFunc::Path_Exists(Media_Path)) {
        PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
        LOGINFO("Recreating %s folder.\n", Media_Path.c_str());
        mkdir(Media_Path.c_str(), 0770);
        std::string Internal_path = DataManager::GetStrValue("tw_internal_path");
        if (!Internal_path.empty()) {
            LOGINFO("Recreating %s folder.\n", Internal_path.c_str());
            mkdir(Internal_path.c_str(), 0770);
        }
#ifdef TW_INTERNAL_STORAGE_PATH
        mkdir(EXPAND(TW_INTERNAL_STORAGE_PATH), 0770);
#endif

        // Afterwards, we will try to set the
        // default metadata that we were hopefully able to get during
        // early boot.
        tw_set_default_metadata(Media_Path.c_str());
        if (!Internal_path.empty())
            tw_set_default_metadata(Internal_path.c_str());

        // Toggle mount to ensure that "internal sdcard" gets mounted
        PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
        PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
    }
}

void TWPartition::Recreate_AndSec_Folder() {
    if (!Has_Android_Secure)
        return;
    LOGINFO("Creating %s: %s\n", Backup_Display_Name.c_str(), Symlink_Path.c_str());
    if (!Mount(true)) {
        gui_msg(Msg(msg::kError, "recreate_folder_err=Unable to recreate {1} folder.")(Backup_Name));
    } else if (!TWFunc::Path_Exists(Symlink_Path)) {
        LOGINFO("Recreating %s folder.\n", Backup_Name.c_str());
        PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
        mkdir(Symlink_Path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
    }
}

uint64_t TWPartition::Get_Max_FileSize() {
    uint64_t maxFileSize = 0;
    const uint64_t constGB = static_cast<uint64_t>(1024) * 1024 * 1024;
    const uint64_t constTB = static_cast<uint64_t>(constGB) * 1024;
    const uint64_t constPB = static_cast<uint64_t>(constTB) * 1024;
    if (Current_File_System == "ext4")
        maxFileSize = 16 * constTB; //16 TB
    else if (Current_File_System == "vfat")
        maxFileSize = 4 * constGB; //4 GB
    else if (Current_File_System == "ntfs" || Current_File_System == "tntfs")
        maxFileSize = 256 * constTB; //256 TB
    else if (Current_File_System == "exfat")
        maxFileSize = 16 * constPB; //16 PB
    else if (Current_File_System == "ext3")
        maxFileSize = 2 * constTB; //2 TB
    else if (Current_File_System == "f2fs" || Current_File_System == "mifs")
        maxFileSize = 3.94 * constTB; //3.94 TB
    else
        maxFileSize = 100000000L;
    return maxFileSize - 1;
}

bool TWPartition::Flash_Image(PartitionSettings *part_settings) {
    std::filesystem::path full_filename = std::filesystem::path(part_settings->Backup_Folder) / Backup_FileName;

    LOGINFO("Image filename is: %s\n", Backup_FileName.c_str());

    if (Backup_Method == BackupMethod::BM_FILES) {
        LOGERR("Cannot flash images to file systems\n");
        return false;
    }
    if (!Can_Flash_Img) {
        LOGERR("Cannot flash images to partitions %s\n", Display_Name.c_str());
        return false;
    }
    if (!Find_Partition_Size()) {
        LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
        return false;
    }
    unsigned long long image_size = TWFunc::Get_File_Size(full_filename);
    if (image_size > Size) {
        LOGINFO("Size (%llu bytes) of image '%s' is larger than target device '%s' (%llu bytes)\n",
                image_size, Backup_FileName.c_str(), Actual_Block_Device.c_str(), Size);
        gui_err("img_size_err=Size of image is larger than target device");
        return false;
    }
    if (Backup_Method == BackupMethod::BM_DD) {
        if (!part_settings->adbbackup) {
            if (Is_Sparse_Image(full_filename)) {
                return Flash_Sparse_Image(full_filename);
            }
        }
        return Raw_Read_Write(part_settings);
    }

    LOGERR("Unknown flash method for '%s'\n", Mount_Point.c_str());
    return false;
}

bool TWPartition::Is_Sparse_Image(const std::string &Filename) {
    uint32_t magic = 0;
    android::base::unique_fd fd(open(Filename.c_str(), O_RDONLY));
    if (!fd.ok()) {
        gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(strerror(errno)));
        return false;
    }

    if (read(fd.get(), &magic, sizeof(magic)) != sizeof(magic)) {
        gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(strerror(errno)));
        return false;
    }
    if (magic == SPARSE_HEADER_MAGIC)
        return true;
    return false;
}

bool TWPartition::Flash_Sparse_Image(const std::string &Filename) {
#ifdef TW_ENABLE_BLKDISCARD
    BlkDiscard();
#endif

    gui_msg(Msg("flashing=Flashing {1}...")(Display_Name));

    int in = open(Filename.c_str(), O_RDONLY);
    if (in < 0) {
        gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(strerror(errno)));
        return false;
    }

    struct sparse_file *s = sparse_file_import(in, true, false);
    if (!s) {
        close(in);
        gui_msg(Msg(msg::kError, "sparse_import_err=Failed to import sparse image '{1}'")(Filename));
        return false;
    }

    int out = open(Actual_Block_Device.c_str(), O_WRONLY);
    if (out < 0) {
        gui_msg(
            Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Actual_Block_Device)(strerror(errno)));
        close(in);
        sparse_file_destroy(s);
        return false;
    }

    int ret = sparse_file_write(s, out, false, false, false);
    close(out);
    close(in);
    sparse_file_destroy(s);
    if (ret < 0) {
        LOGERR("Failed to flash sparse image '%s' to '%s'\n", Filename.c_str(), Actual_Block_Device.c_str());
        return false;
    }
    return true;
}

void TWPartition::Change_Mount_Read_Only(bool new_value) {
    Mount_Read_Only = new_value;
}

bool TWPartition::Is_Read_Only() {
    return Mount_Read_Only;
}

int TWPartition::Check_Lifetime_Writes() {
    bool original_read_only = Mount_Read_Only;
    int ret = 1;

    Mount_Read_Only = true;
    if (Mount(false)) {
        Find_Actual_Block_Device();
        std::string temp = Actual_Block_Device;
        Find_Real_Block_Device(temp, false);
        std::string block = basename(temp.c_str());
        std::filesystem::path file = std::filesystem::path("/sys/fs") / Current_File_System / block / "lifetime_write_kbytes";

        if (std::error_code ec; std::filesystem::exists(file, ec)) {
        std::string result;
            if (TWFunc::read_file(file, result) != 0) {
                LOGINFO("Check_Lifetime_Writes of '%s' failed to read_file\n", file.c_str());
            } else {
                LOGINFO("Check_Lifetime_Writes result: '%s'\n", result.c_str());
                if (result == "0") {
                    ret = 0;
                }
            }
        } else {
            LOGINFO("Check_Lifetime_Writes file does not exist '%s'\n", file.c_str());
        }
        UnMount(true);
    } else {
        LOGINFO("Check_Lifetime_Writes failed to mount '%s'\n", Mount_Point.c_str());
    }
    Mount_Read_Only = original_read_only;
    return ret;
}

int TWPartition::Decrypt_Adopted() {
#ifdef TW_INCLUDE_CRYPTO
    int ret = 1;
    Is_Adopted_Storage = false;
    std::string Adopted_Key_File = "";

    if (!Removable)
        return ret;

    android::base::unique_fd fd(open(Alternate_Block_Device.c_str(), O_RDONLY));
    if (!fd.ok()) {
        LOGINFO("failed to open '%s'\n", Alternate_Block_Device.c_str());
        return ret;
    }
    char type_guid[80];
    char part_guid[80];

    uint32_t p_num;
    size_t last_digit = Primary_Block_Device.find_last_not_of("0123456789");
    if ((last_digit != std::string::npos) && (last_digit != Primary_Block_Device.length() - 1))
        p_num = atoi(Primary_Block_Device.substr(last_digit + 1).c_str()) + 1;
    else
        p_num = 2;

    if (gpt_disk_get_partition_info(fd.get(), p_num, type_guid, part_guid) == 0) {
        LOGINFO("type: '%s'\n", type_guid);
        LOGINFO("part: '%s'\n", part_guid);
        Adopted_GUID = part_guid;
        LOGINFO("Adopted_GUID '%s'\n", Adopted_GUID.c_str());
        if (std::string_view(type_guid) == TWGptAndroidExpand) {
            LOGINFO("android_expand found\n");
            Adopted_Key_File = std::format("/data/misc/vold/expand_{}.key", part_guid);
            if (TWFunc::Path_Exists(Adopted_Key_File)) {
                Is_Adopted_Storage = true;
                /* Until we find a use case for this, I think it is safe
                 * to disable USB Mass Storage whenever adopted storage
                 * is present.
                 */
                if (p_num == 2) {
                    // TODO: Properly detect mixed vs fully adopted storage. Maybe this
                    // should be moved to partitionmanager instead, and disable after
                    // checking all partitions. Also the presence of adopted storage does
                    // not necessarily mean it's being used as Internal Storage
                    LOGINFO("Detected adopted storage, disabling USB mass storage mode\n");
                    DataManager::SetValue("tw_has_usb_storage", 0);
                }
            }
        }
    }

    if (Is_Adopted_Storage) {
        std::string Adopted_Block_Device = Alternate_Block_Device + "p" + TWFunc::to_string(p_num);
        if (!TWFunc::Path_Exists(Adopted_Block_Device)) {
            Adopted_Block_Device = Alternate_Block_Device + TWFunc::to_string(p_num);
            if (!TWFunc::Path_Exists(Adopted_Block_Device)) {
                LOGINFO("Adopted block device does not exist\n");
                return ret;
            }
        }
        LOGINFO("key file is '%s', block device '%s'\n", Adopted_Key_File.c_str(), Adopted_Block_Device.c_str());
        char crypto_blkdev[MAXPATHLEN];
        std::string thekey;
        int fdkey = open(Adopted_Key_File.c_str(), O_RDONLY);
        if (fdkey < 0) {
            LOGINFO("failed to open key file\n");
            return ret;
        }
        char buf[512];
        ssize_t n;
        while ((n = read(fdkey, &buf[0], sizeof(buf))) > 0) {
            thekey.append(buf, n);
        }
        close(fdkey);
        // unsigned char* key = (unsigned char*) thekey.data();
        // cryptfs_revert_ext_volume(part_guid);

        // ret = cryptfs_setup_ext_volume(part_guid, Adopted_Block_Device.c_str(), key, thekey.size(), crypto_blkdev);
        if (ret == 0) {
            LOGINFO("adopted storage new block device: '%s'\n", crypto_blkdev);
            Decrypted_Block_Device = crypto_blkdev;
            Is_Decrypted = true;
            Is_Encrypted = true;
            Find_Actual_Block_Device();
            if (!Mount_Storage_Retry(false)) {
                LOGERR("Failed to mount decrypted adopted storage device\n");
                Is_Decrypted = false;
                Is_Encrypted = false;
                // cryptfs_revert_ext_volume(part_guid);
                ret = 1;
            } else {
                UnMount(false);
                Has_Android_Secure = false;
                Symlink_Path = "";
                Symlink_Mount_Point = "";
                Backup_Name = Mount_Point.substr(1);
                Backup_Path = Mount_Point;
                TWPartition *sdext = PartitionManager.Find_Partition_By_Path("/sd-ext");
                if (sdext && sdext->Actual_Block_Device == Adopted_Block_Device) {
                    LOGINFO("Removing /sd-ext from partition list due to adopted storage\n");
                    PartitionManager.Remove_Partition_By_Path("/sd-ext");
                }
                Setup_Data_Media();
                Wipe_Available_in_GUI = true;
                Wipe_During_Factory_Reset = true;
                Can_Be_Backed_Up = true;
                Can_Encrypt_Backup = true;
                Use_Userdata_Encryption = true;
                Is_Storage = true;
                Storage_Name = "Adopted Storage";
                Is_SubPartition = true;
                SubPartition_Of = "/data";
                PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
                DataManager::SetValue("tw_has_adopted_storage", 1);
            }
        } else {
            LOGERR("Failed to setup adopted storage decryption\n");
        }
    }
    return ret;
#else
    LOGINFO("Decrypt_Adopted: no crypto support\n");
    return 1;
#endif
}

void TWPartition::Revert_Adopted() {
#ifdef TW_INCLUDE_CRYPTO
    if (!Adopted_GUID.empty()) {
        PartitionManager.Remove_MTP_Storage(Mount_Point);
        UnMount(false);
        // cryptfs_revert_ext_volume(Adopted_GUID.c_str());
        Is_Adopted_Storage = false;
        Is_Encrypted = false;
        Is_Decrypted = false;
        Decrypted_Block_Device = "";
        Find_Actual_Block_Device();
        Wipe_During_Factory_Reset = false;
        Can_Be_Backed_Up = false;
        Can_Encrypt_Backup = false;
        Use_Userdata_Encryption = false;
        Is_SubPartition = false;
        SubPartition_Of = "";
        Has_Data_Media = false;
        Storage_Path = Mount_Point;
        if (!Symlink_Mount_Point.empty()) {
            TWPartition *Dat = PartitionManager.Find_Partition_By_Path("/data");
            if (Dat) {
                Dat->UnMount(false);
                Dat->Symlink_Mount_Point = Symlink_Mount_Point;
            }
            Symlink_Mount_Point = "";
        }
    }
#else
    LOGINFO("Revert_Adopted: no crypto support\n");
#endif
}

void TWPartition::Set_Backup_FileName(std::string fname) {
    Backup_FileName = fname;
}

std::string TWPartition::Get_Backup_Name() {
    return Backup_Name;
}

std::string TWPartition::Get_Mount_Point() {
    return Mount_Point;
}

void TWPartition::Set_Block_Device(std::string block_device) {
    Primary_Block_Device = Actual_Block_Device = block_device;
}

bool TWPartition::Get_Super_Status() {
    return Is_Super;
}

void TWPartition::Set_Can_Be_Backed_Up(bool val) {
    Can_Be_Backed_Up = val;
}

void TWPartition::Set_Can_Be_Wiped(bool val) {
    Can_Be_Wiped = val;
    Wipe_Available_in_GUI = val;
}

std::string TWPartition::Get_Backup_FileName() {
    return Backup_FileName;
}

std::string TWPartition::Get_Display_Name() {
    return Display_Name;
}

bool TWPartition::Is_SlotSelect() {
    return SlotSelect;
}
