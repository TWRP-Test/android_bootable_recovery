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

#ifndef TWRP_PARTITION_MANAGER
#define TWRP_PARTITION_MANAGER

#include <sys/poll.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "exclude.hpp"
#include "progresstracking.hpp"

#ifdef TW_INCLUDE_CRYPTO
#include "fscrypt_policy.h"
#endif

#define REPACK_ORIG_DIR "/tmp/repackorig/"
#define REPACK_NEW_DIR "/tmp/repacknew/"

// BasePartition is used for overriding so we can run custom, device
// specific code.
class BasePartition {
public:
  BasePartition() = default;

  virtual ~BasePartition() = default;

  virtual bool PreWipeEncryption() {
    return true;
  }

  virtual bool PostWipeEncryption() {
    return true;
  }
};

BasePartition* MakePartition();

struct PartitionList {
  std::string display_name;
  std::string mount_point;
  bool selected;
};

struct UeventBlockData {
  std::string action;
  std::string subsystem;
  std::string block_device;
  std::string type;
  std::string sysfs_path;
};

struct FlagsMap {
  std::string primary_block_device;
  std::string alternate_block_device;
  std::string file_system;
  std::string flags;
  std::string fstab_line;
};

// Operation direction for RawReadWrite(): backup reads from the block
// device, restore writes to it.
enum class PartitionManagerOp {
  kBackup = 0,
  kRestore = 1,
};

class TWPartition;

namespace gui2_backend {
class twrp_tools_backend;
class twrp_mount_backend;
}  // namespace gui2_backend

// Settings for backup session
struct PartitionSettings {
  // Partition to pass to the partition backup loop
  TWPartition* part = nullptr;
  // Path to restore folder
  std::string backup_folder;
  // tell system to create digest for partitions
  bool generate_digest = false;
  // Total size of restored backup
  uint64_t total_restore_size = 0;
  // remaining img/emmc bytes to backup for progress indicator
  uint64_t img_bytes_remaining = 0;
  // remaining file bytes to backup for progress indicator
  uint64_t file_bytes_remaining = 0;
  // used to calculate how fast we backup images
  uint64_t img_time = 0;
  // used to calculate how fast we backup files
  uint64_t file_time = 0;
  // total image bytes of all emmc partitions
  uint64_t img_bytes = 0;
  // total file bytes of all file based partitions
  uint64_t file_bytes = 0;
  // Number of partitions to restore
  int partition_count = 0;
  // Keep track of progress in GUI
  ProgressTracking* progress = nullptr;
  // Current operation of backup or restore
  PartitionManagerOp pm_method = PartitionManagerOp::kBackup;
};

enum class BackupMethod {
  kNone = 0,
  kFiles = 1,
  kDd = 2,
};

// Custom twrp fstab flags, matched against the tw_flags[] table in partition.cpp
enum class TWFlag {
  // Retain position
  kDefaults,
  kAndSec,
  kBackup,
  kBackupName,
  kBlockSize,
  kCanBeWiped,
  kCanEncryptBackup,
  kDisplay,
  kEncryptable,
  kFileEncryption,
  kMetadataEncryption,
  kFlashImg,
  kForceEncrypt,
  kFsFlags,
  kIgnoreBlkid,
  kLength,
  kMountToDecrypt,
  kQuota,
  kRemovable,
  kSettingsStorage,
  kStorage,
  kStorageName,
  kSubPartitionOf,
  kSymlink,
  kUserDataEncryptBackup,
  kUserMrf,
  kWipeDuringFactoryReset,
  kWipeInGui,
  kSlotSelect,
  kWait,
  kVerify,
  kCheck,
  kAltDevice,
  kNoTrim,
  kVoldManaged,
  kFormattable,
  kResize,
  kKeyDirectory,
  kWrappedKey,
  kAdoptedMountDelay,
  kDmUseOriginalPath,
  kFsCompress,
  kLogical,
  kMetadataCsum,
};

// Partition class
class TWPartition {
public:
  TWPartition();

  virtual ~TWPartition() = default;

  // Checks mount to see if the partition is currently mounted
  [[nodiscard]] bool IsMounted() const;

  // Checks if the root directory of the file system can be written to
  [[nodiscard]] bool IsFileSystemWritable() const;

  // Mounts the partition if it is not mounted
  bool Mount(bool display_error);

  // Unmounts the partition if it is mounted
  bool Unmount(bool display_error, int flags = 0) const;

  // Remounts the partition
  bool Remount(bool display_error);

  // Remounts the partition with read/write access
  bool RemountRw(bool display_error);

  // Bind mount partition if symlink mountpoint is populated
  bool BindMount() const;

  // Clear the partition using BLKDISCARD ioctl
  bool BlkDiscard() const;

  // Wipes the partition
  bool Wipe(const std::string& new_file_system);

  // Wipes the partition
  bool Wipe();

  // Wipes android secure
  bool WipeAndSec();

  // Wipe /data/cache with devices that have no cache partition
  bool WipeDataCache();

  // Checks to see if we have everything needed to be able to repair the current file system
  [[nodiscard]] bool CanRepair() const;

  // get partition max file size
  [[nodiscard]] uint64_t GetMaxFileSize() const;

  // Repairs the current file system
  bool Repair();

  // Checks to see if we have everything needed to be able to resize the current file system
  [[nodiscard]] bool CanResize() const;

  // Resizes the current file system
  bool Resize();

  // Backs up the partition to the folder specified
  bool Backup(PartitionSettings* part_settings, pid_t* tar_fork_pid);

  // Restores the partition using the backup folder provided
  bool Restore(PartitionSettings* part_settings);

  // Returns the overall restore size of the backup
  uint64_t GetRestoreSize(PartitionSettings* part_settings);

  // Returns a string of the backup method for human readable output
  [[nodiscard]] std::string BackupMethodByName() const;

  // Ignores wipe commands for /data/media devices and formats the original block device
  bool WipeEncryption();

  // Checks the filesystem type using blkid
  void CheckFsType();

  bool UpdateSize(bool display_error, bool defer_folder_size = false);

  // Updates size information, defer_folder_size leaves the data/media walk to UpdateDataSizeAsync()
  // Starts a deferred data/media size scan
  void UpdateDataSizeAsync();

  // Applies a completed scan on the caller thread
  void ApplyAsyncDataSize();

  // True while user 0's CE storage is still locked
  [[nodiscard]] static bool DataIsLocked();

  // Recreates the /data/media folder
  void RecreateMediaFolder();

  // Flashes an image to the partition
  bool FlashImage(const PartitionSettings* part_settings);

  // Changes mount_read_only_ to new_value
  void ChangeMountReadOnly(bool new_value);

  // Check if system is read-only in TWRP
  [[nodiscard]] bool IsReadOnly() const;

  int CheckLifetimeWrites();

  // Apply partition specific settings after fstab processed
  void PartitionPostProcessing(bool display_error);

  // Set backup filename for partition
  void SetBackupFileName(const std::string& filename);

  // Get the backup filename for the partition
  std::string GetBackupFileName();

  // Get backup_name_ for partition
  std::string GetBackupName();

  // If FBE is present, backup exclusions are set up and DE decrypt is attempted
  bool DecryptFbeDe();

  // Return mount_point_ or directory the current partition is mounted on
  std::string GetMountPoint();

  // Returns true if this partition is a super volume
  [[nodiscard]] bool GetSuperStatus() const;

  // Update whether the partition can be backed up or not
  void SetCanBeBackedUp(bool val);

  // Update whether the partition can be wiped or not
  void SetCanBeWiped(bool val);

  // Get the display name in the gui for the partition
  std::string GetDisplayName();

  // Return whether the partition is a slot partition or not
  [[nodiscard]] bool IsSlotSelect() const;

  // Current file system
  std::string current_file_system_;
  // Actual block device (one of primary, alternate, or decrypted)
  std::string actual_block_device_;
  // By-name path saved before FindRealBlockDevice resolves symlinks, for slot_select suffix logic
  std::string original_block_device_;
  // Name displayed in the partition list for backup selection
  std::string backup_display_name_;
  // Indicates if the partition is currently present as a block device
  bool is_present_;
  unsigned int mtp_storage_id_;

protected:
  // Indicates presence of /data/media, may affect wiping and backup methods
  bool has_data_media_;

  // Sets up a partition as a /data/media emulated storage partition
  void SetupDataMedia();

  // Allow super partition setup to change block device
  void SetBlockDevice(const std::string& block_device);

private:
  // Processes a fstab line
  bool ProcessFstabLine(const char* fstab_line, bool display_error,
                        std::map<std::string, FlagsMap>* twrp_flags);

  // Overrides block devices from the twrp.flags file
  void OverrideBlockDevicesFromFlags(int fstab_version, bool display_error,
                                     std::map<std::string, FlagsMap>* twrp_flags);

  // Applies sysfs and wildcard block device attributes
  void ApplyBlockDeviceAttributes(std::string_view line);

  // Classifies the partition by mount point and file system type
  bool ClassifyByMountPoint(bool display_error);

  void ApplyTwrpFlagsFile(int fstab_version, bool display_error,
                          std::map<std::string, FlagsMap>* twrp_flags);

  // Processes flags from the /etc/twrp.flags file
  // Mounts the persist root partition if applicable
  void MountPersistRootIfNeeded();

  // Setup data partition after fstab processed
  void SetupDataPartition(bool display_error);

  // Set FBE status of partition
  void SetFbeStatus();

  // Setup cache partition after fstab processed
  void SetupCachePartition(bool display_error);

  // Searches for and finds wildcard block devices
  bool FindWildcardBlockDevices(const std::string& device);

  // Determines the correct block device and stores it in actual_block_device_
  void FindActualBlockDevice();

  // Apply custom twrp fstab flags
  void ApplyTwFlag(TWFlag flag, std::string_view str, bool val);

  // Process custom twrp fstab flags
  void ProcessTwFlags(std::string_view flags, bool display_error, int fstab_ver);

  // Process standard fstab fs flags
  void ProcessFsFlags(std::string_view str);

  // Saves fs flags to a vector in case there are multiple lines in a v2 fstab with different mount flags for different file systems
  void SaveFsFlags(const std::string& local_File_System, int local_Mount_Flags,
                   const std::string& local_Mount_Options);

  // Checks to see if the file system given is considered a file system
  static bool IsFileSystem(std::string_view file_system);

  // Checks to see if the file system given is considered an image
  static bool IsImage(std::string_view file_system);

  // Sets defaults for a file system partition
  void SetupFileSystem(bool display_error);

  // Sets defaults for an image partition
  void SetupImage();

  // Sets up .android_secure settings
  void SetupAndSec();

  void FindRealBlockDevice(string& block_device);

  // Checks the block device given and follows symlinks until it gets to the real block device
  // Finds the partition size using ioctl
  uint64_t IoctlGetBlockSize();

  // Finds the partition size from /proc/partitions
  bool FindPartitionSize();

  // Ensure that a subdirectory is unmounted before unmounting this partition
  static bool EnsureSubdirectoryUnmounted(const char* mount_point);

  // Create an ext2/ext3/ext4 filesystem
  bool WipeExtfs(const std::string& file_system);

  // Formats using ext4, uses make_ext4fs when present
  bool WipeExt4();

  // Formats as FAT if mkfs.fat exits otherwise rm -rf wipe
  bool WipeFat();

  // Formats as EXFAT
  bool WipeExfat();

  // Uses rm -rf to wipe
  bool WipeRmrf();

  // Uses make_f2fs to wipe
  bool WipeF2fs();

  // Uses mkntfs to wipe
  bool WipeNtfs();

  // Uses rm -rf to wipe but does not wipe /data/media
  bool WipeDataWithoutWipingMedia();

  // Uses rm -rf to wipe but does not wipe /data/media
  bool WipeDataWithoutWipingMediaFunc(const std::filesystem::path& parent);

  // Backs up using tar for file systems
  bool BackupTar(PartitionSettings* part_settings, pid_t* tar_fork_pid);

  // Backs up using raw read/write for emmc memory types
  bool BackupImage(PartitionSettings* part_settings);

  bool RawReadWrite(const PartitionSettings* part_settings) const;

  std::string GetRestoreFileSystem(PartitionSettings* part_settings);

  // Returns the file system that was in place at the time of the backup
  // Restore using tar for file systems
  bool RestoreTar(PartitionSettings* part_settings);

  // Restore using dd for images
  bool RestoreImage(PartitionSettings* part_settings);

  // Get Partition size, used, and free space using statfs
  bool GetSizeViaStatfs(bool display_error);

  // Get Partition size, used, and free space using df command
  bool GetSizeViaDf(bool display_error);

  // Creates a directory if it doesn't already exist
  static bool MakeDir(const std::string& path, bool display_error);

  // Recreates the .android_secure folder
  void RecreateAndSecFolder();

  bool MountStorageRetry(bool display_error);

  // Tries multiple times with a half second delay to mount a device in case storage is slow to mount
  // Determines if a file is in sparse image format
  static bool IsSparseImage(const std::string& Filename);

  // Flashes a sparse image using libsparse
  bool FlashSparseImage(const std::string& Filename) const;

  // Adds an exclusion for path to both the backup and wipe exclusion lists
  void ExcludeAll(const std::string& path);

  // Indicates that the partition can be mounted
  bool can_be_mounted_;
  // Indicates that the partition can be wiped
  bool can_be_wiped_;
  // Indicates that the partition will show up in the backup list
  bool can_be_backed_up_;
  // Indicates that the partition will always be formatted w/ "rm -rf *"
  bool use_rm_rf_;
  // Indicates that this partition is wiped during a factory reset
  bool wipe_during_factory_reset_;
  // Indicates that the wipe can be user initiated in the GUI system
  bool wipe_available_in_gui_;
  bool is_sub_partition_;
  // Indicates that this partition is a sub-partition of another partition (e.g. datadata is a sub-partition of data)
  // Indicates that this partition has a sub-partition
  bool has_sub_partition_;
  std::string sub_partition_of_;
  // Indicates which partition is the parent partition of this partition (e.g. /data is the parent partition of /datadata)
  // Symlink path (e.g. /data/media)
  std::string symlink_path_;
  // /sdcard could be the symlink mount point for /data/media
  std::string symlink_mount_point_;
  // Mount point for this partition (e.g. /system or /data)
  std::string mount_point_;
  // Path for backup
  std::string backup_path_;
  // If the block device contains an asterisk, we set this flag
  bool wildcard_block_device_;
  std::string sysfs_entry_;
  // For v2 fstab, if the "block device" starts with /devices then it is a sysfs entry that is handled by uevents
  // Block device (e.g. /dev/block/mmcblk1p1)
  std::string primary_block_device_;
  // Alternate block device (e.g. /dev/block/mmcblk1)
  std::string alternate_block_device_;
  // Decrypted block device available after decryption
  std::string decrypted_block_device_;
  bool removable_;
  // Indicates if this partition is removable -- affects how often we check overall size, if present, etc.
  int length_;
  // Used by make_ext4fs to leave free space at the end of the partition block for things like a crypto footer
  // Overall size of the partition
  uint64_t size_;
  // Overall used space
  uint64_t used_;
  // Overall free space
  uint64_t free_;
  uint64_t backup_size_;
  // Backup size -- may be different than used space especially when /data/media is present
  // backup_size_ is the statfs figure, not the walked one
  bool backup_size_provisional_;
  // Restore size of the current restore operation
  uint64_t restore_size_ = 0;
  bool can_be_encrypted_;
  // This partition might be encrypted, affects error handling, can only be true if crypto support is compiled in
  bool is_encrypted_;
  // This partition is thought to be encrypted -- it wouldn't mount for some reason, only available with crypto support
  // This partition has successfully been decrypted
  bool is_decrypted_;
  // File Based Encryption is present
  bool is_fbe_;
  // Existing device/DE keys installed on this mount
  bool de_keys_ready_ = false;
  bool RestoreDeKeys();
  bool mount_to_decrypt_;
  // Mount this partition during decrypt (/vendor, /firmware, etc in case we need proprietary libs or firmware files)
  // Display name for the GUI
  std::string display_name_;
  // Backup name -- used for backup filenames
  std::string backup_name_;
  // Name displayed in the partition list for storage selection
  std::string storage_name_;
  // Actual backup filename
  std::string backup_file_name_;
  // Method used for backup
  BackupMethod backup_method_;
  // Indicates if this item can be encrypted during backup
  bool can_encrypt_backup_;
  // Indicates if we will use userdata encryption splitting on an encrypted backup
  bool use_userdata_encryption_;
  // Indicates the presence of .android_secure on this partition
  bool has_android_secure_;

  // Indicates if this partition is used for storage for backup, restore, and installing zips
  bool is_storage_;

  bool is_settings_storage_;
  // Indicates that this storage partition is the location of the .twrps settings file and the location that is used for custom themes
  std::string storage_path_;
  // Indicates the path to the storage -- root indicates mount point, media/ indicates e.g. /data/media
  // File system from the recovery.fstab
  std::string fstab_file_system_;
  // File system flags from recovery.fstab
  int mount_flags_;
  // File system options from recovery.fstab
  std::string mount_options_;
  // Block size for formatting
  uint64_t format_block_size_;
  // Ignore blkid results due to superblocks lying to us on certain devices / partitions
  bool ignore_blkid_;

  // Indicates if this partition can have images flashed to it via the GUI
  bool can_flash_img_;
  // Only mount this partition as read-only
  bool mount_read_only_;
  // Indicates that this partition is for adopted storage (android_expand)
  bool is_adopted_storage_;
  // Partition has A/B slots
  bool slot_select_;
  // Exclusions for file based backups
  TWExclude backup_exclusions;
  // Exclusions for file based wipes (data/media devices only)
  TWExclude wipe_exclusions;
  // Metadata key directory needed for mounting FBE encrypted data partitions using metadata encryption
  std::string key_directory_;

  bool needs_fs_compress_;
  bool needs_metadata_csum_;

  struct PartitionFsFlagsStruct {
    // This struct is used to store mount flags and options for different file systems for the same partition
    std::string file_system;
    int mount_flags;
    std::string mount_options;
  };

  std::vector<PartitionFsFlagsStruct> fs_flags;
  // This vector stores mount flags and options for different file systems for the same partition
  // States whether partition should be loaded from the super partition
  bool is_super_;

  friend class TWPartitionManager;
  friend class DataManager;
  // gui2 backends render the same partition details as gui/action.cpp.
  friend class gui2_backend::twrp_tools_backend;
  // The mount page owns explicit Data remounts and reads this state to report
  // whether CE credentials are still required.
  friend class gui2_backend::twrp_mount_backend;
  friend class GUIPartitionList;
  friend class GUIAction;
  friend class PageManager;
};

struct UsersStruct {
  int user_id = 0;
  std::string user_name;
  int type;
  bool is_decrypted;
};

class TWPartitionManager {
public:
  // Constructor for TWPartitionManager
  TWPartitionManager();

  ~TWPartitionManager() = default;

  // Parses the fstab files
  int ProcessFstab(std::string fstab_filename, bool display_error, bool recovery_mode);

  // Populates the partitions
  void SetupFstabPartitions(bool display_error);

  // Creates /etc/fstab file that's used by the command line for mount commands
  int WriteFstab() const;

  // Decrypt Data if enabled
  void DecryptData();

  // Outputs partition information to the log
  void OutputPartitionLogging() const;

  // Outputs partition details to the log
  static void OutputPartition(const TWPartition* part);

  // Mounts partition based on path (e.g. /system)
  int MountByPath(const std::string& Path, bool display_error) const;

  // Unmounts partition based on path
  bool UnmountByPath(const std::string& path, bool display_error,
                                   int flags = 0) const;

  // Checks if partition is mounted based on path
  [[nodiscard]] int IsMountedByPath(const std::string& Path) const;

  // Mounts the current storage location
  int MountCurrentStorage(bool display_error) const;

  // Mounts the settings file storage location (usually internal)
  int MountSettingsStorage(bool display_error) const;

  // Returns a pointer to a partition based on path
  [[nodiscard]] TWPartition* FindPartitionByPath(const std::string& Path) const;

  // Returns a pointer to a partition based on block device
  [[nodiscard]] TWPartition* FindPartitionByBlockDevice(const std::string& block_device) const;

  // True if backup_name has valid length and characters, and (when must_be_unique) no
  // backup folder with that name already exists.
  [[nodiscard]] static bool IsBackupNameValid(const std::string& backup_name, bool display_error,
                                              bool must_be_unique);

  // Initiates a backup in the current storage
  bool RunBackup();

  // Restores a backup
  int RunRestore(const std::string& restore_name) const;

  void SetRestoreFiles(const std::string& restore_name) const;

  // Used to gather a list of available backup partitions for the user to select for a restore
  // Wipes a partition based on path
  int WipeByPath(const std::string& Path) const;

  // Wipes a partition based on path
  int WipeByPath(const std::string& Path, const std::string& new_file_system) const;

  // Performs a factory reset
  int FactoryReset() const;

  // Wipes dalvik cache
  int WipeDalvikCache() const;

  // Wipes rotation data --
  int WipeRotateData() const;

  // Wipe battery stats -- /data/system/batterystats.bin
  int WipeBatteryStats() const;

  // Wipes android secure
  int WipeAndroidSecure() const;

  // Really formats data on /data/media devices -- also removes encryption
  int FormatData();

  // Removes and recreates the media folder on /data/media devices
  int WipeMediaFromData() const;

  // Repairs a partition based on path
  int RepairByPath(const std::string& Path, bool display_error) const;

  // Resizes a partition based on path
  int ResizeByPath(const std::string& Path, bool display_error) const;

  // Updates fstab, file systems, sizes, etc.
  void UpdateSystemDetails(bool defer_data_size = false, bool display_error = true) const;

  // Applies a completed background data-size calculation
  void ProcessAsyncDataSize() const;

  // Attempt to decrypt any encrypted partitions
  int DecryptDevice(const std::string& Password, int user_id = 0);

  // Data was unmounted, so its fscrypt keys went with it
  void MarkDataLocked();

  // Checks whether a partition is already showing this name
  [[nodiscard]] bool StorageNameInUse(const std::string& Name);

  // Parse FBE users
  void ParseUsers();

  // Enable USB storage mode
  int UsbStorageEnable();

  // Disable USB storage mode
  int UsbStorageDisable();

  // Mounts all storage locations
  void MountAllStorage() const;

  // Unmounts system and data if not data/media and boot if boot is mountable
  void UnmountMainPartitions() const;

  // Repartitions the sdcard
  int PartitionSdCard() const;

  // Returns a pointer to a default storage partition
  [[nodiscard]] TWPartition* GetDefaultStoragePartition() const;

  // Returns the value of stop_backup
  [[nodiscard]] int CheckBackupCancel() const;

  // Signals partition backup to cancel
  int CancelBackup();

  // Clean Backup Folder on Error
  static void CleanBackupFolder(const std::string& backup_folder);

  int FixContexts() const;

  void GetPartitionList(const std::string& list_type,
                        std::vector<PartitionList>* partition_list) const;

  // Indicates if the fstab has been processed or not
  [[nodiscard]] bool FstabProcessed() const;

  void OutputStorageFstab() const;

  // Creates a /cache/recovery/storage.fstab file with a list of all potential storage locations for app use
  // Enables MTP
  bool EnableMtp();

  // Adds all storage objects for MTP
  void AddAllMtpStorage() const;

  // Disables MTP
  bool DisableMtp();

  // Adds or removes an MTP Storage partition
  static bool AddMtpStorage(const std::string& mount_point);

  // Adds or removes an MTP Storage partition
  static bool AddMtpStorage(unsigned int storage_id);

  // Adds or removes an MTP Storage partition
  static bool RemoveMtpStorage(const std::string& mount_point);

  // Adds or removes an MTP Storage partition
  static bool RemoveMtpStorage(unsigned int storage_id);

  static void TranslatePartition(const std::string& path, const std::string& resource_name,
                                 const std::string& default_value);

  static void TranslatePartition(const std::string& path, const std::string& resource_name,
                                 const std::string& default_value,
                                 const std::string& storage_resource_name,
                                 const std::string& storage_default_value);

  static void TranslatePartition(const std::string& path, const std::string& resource_name,
                                 const std::string& default_value,
                                 const std::string& storage_resource_name,
                                 const std::string& storage_default_value,
                                 const std::string& backup_name, const std::string& backup_default);

  // Updates display names based on translations
  void TranslatePartitionDisplayNames() const;

  // Removes / erases a partition entry from the partition list
  void RemovePartitionByPath(const std::string& Path);

  // Prepare all known super volumes from super partition
  bool PrepareAllSuperVolumes();

  bool FlashImage(const std::string& path, const std::string& filename) const;

  // Flashes an image to a selected partition from the partition list
  // Restore the partitions based on type
  bool RestorePartition(PartitionSettings* part_settings) const;

  std::atomic_bool stop_backup;

  // Override the active slot for repacking
  void OverrideActiveSlot(const std::string& slot);

  // Sets the active slot to A or B
  void SetActiveSlot(const std::string& slot);

  // Returns active slot _a or _b
  [[nodiscard]] std::string GetActiveSlotSuffix() const;

  // Returns active slot A or B for display purposes
  std::string GetActiveSlotDisplay();

  // Returns path of ANDROID_ROOT environment variable
  static std::string GetAndroidRootPath();

  // Used for uevent code
  pollfd uevent_pfd{};

  void RemoveUeventDevices(const std::string& sysfs_path);

  // Removes subpartitions from the partitions_ vector for a matched uevent device
  // Handle uevent data
  void HandleUevent(const UeventBlockData& uevent_data);

  // Opens the uevent netlink socket
  void SetupUevent();

  static UeventBlockData GetEventBlockValues(char* buf, int len);

  // Scans the buffer from uevent data and loads the appropriate data into a UeventBlockData struct for processing
  // Reads uevent data into a buffer
  void ReadUevent() const;

  // Closes the uevent netlink socket
  void CloseUevent();

  // Adds a new partition to the partitions_ vector
  void AddPartition(TWPartition* part);

  static std::string GetBarePartitionName(const std::string& mountpoint);

  // One pass over an fstab file, creating a partition per entry; the
  // parse_userdata pass replaces /data and /metadata from the vendor fstab
  bool ProcessFstabFile(const std::string& fstab_filename, const bool parse_userdata,
                        const bool display_error, std::map<std::string, FlagsMap>& twrp_flags);

  // Adds twrp.flags entries whose mount point has no partition yet
  void AddRemainingTwrpFlags(const std::map<std::string, FlagsMap>& twrp_flags,
                             const bool display_error);

  // Prepare logical super partition volume for mounting
  bool PrepareSuperVolume(TWPartition* twrpPart);

  // Get Super Partition block device path
  static std::string GetSuperPartition();

  // Setup logical dm devices on super partition
  static void SetupSuperDevices();

  // Return whether device has a super partition
  [[nodiscard]] static bool GetSuperStatus();

  // Setup the super partition for backup and restore
  void SetupSuperPartition();

  // Recreate TWRP_AB_LOGS_DIR after wipe
  static bool RecreateLogsDir();

  // Returns pointer to list of users
  std::vector<UsersStruct>* GetUsersList();

  // Sets encryption state for devices (ro.crypto.state)
  static void SetCryptoState();

  static int SetCryptoType(const std::string& crypto_type);

  // Sets encryption type for FDE (block) and FBE (file) devices (ro.crypto.type)
  // Unlock all block devices after update_engine runs
  static void UnlockBlockPartitions();

  // Unmap super devices in TWRP
  bool UnmapSuperDevices();

  // Check and run pending merges on data for VAB devices
  bool CheckPendingMerges();

  // Disable AVB 2.0
  static bool DisableAvb2(bool display_info);

private:
  // Matches a partition by mount point or symlink mount point (e.g. /sdcard);
  // member access relies on the TWPartition friendship
  static bool MatchesPath(const TWPartition* p, const std::string& path);

  // Sets up settings storage
  static void SetupSettingsStoragePartition(const TWPartition* part);

  // Sets up .android_secure if needed
  static void SetupAndroidSecureLocation(TWPartition* part);

  // Backup the partitions based on type
  bool BackupPartition(PartitionSettings* part_settings);

  [[nodiscard]] TWPartition* FindPartitionByMtpStorageId(unsigned int storage_id) const;

  // Returns a pointer to a partition based on MTP Storage ID
  // Adds or removes an MTP Storage partition
  bool AddRemoveMtpStorage(const TWPartition* part, int message_type) const;

  [[nodiscard]] TWPartition*
  FindNextStorage(const std::string& Path, bool exclude_data_media) const;

  int OpenLunFile(const std::string& partition_path,
                                const std::string& lun_file) const;

  // Completes various post-decrypt tasks
  void PostDecrypt(const std::string& block_device) const;

  static void ColdbootScan(std::span<const std::string> sysfs_entries,
                           const std::filesystem::path& path,
                           int depth);

  // Scans subfolders to find matches to the paths stored in sysfs_entries so we can trigger the uevent system to "re-add" devices
  // Starts the scan of the /sys/block folder
  void Coldboot() const;

  static bool PrepareEmptyFolder(const std::string& Folder);

  // Creates an empty folder at Folder. If the folder already exists, the folder is deleted, then created
#ifdef TW_HAS_MTP
  pid_t mtp_pid_ = 0;
#endif
  bool mtp_was_enabled;
  int mtp_write_fd;
  // PID of twrpTar fork
  pid_t tar_fork_pid = 0;
  // Ramdisk format of boot partition
  std::string original_ramdisk_format;

  // Marks given user ID in users_list_ as decrypted
  void MarkUserDecrypted(int user_id);

  // Checks to see if all users are decrypted
  void CheckUsersDecryptionStatus() const;

  // Vector list of all partitions
  std::vector<TWPartition*> partitions_;
  // Current Active Slot (A or B) for display purposes
  std::string active_slot_display_;
  // List of FBE users
  std::vector<UsersStruct> users_list_;
  // Display value for super partitions
  std::vector<std::string> super_partition_list_;
};

extern TWPartitionManager PartitionManager;

#endif // TWRP_PARTITION_MANAGER