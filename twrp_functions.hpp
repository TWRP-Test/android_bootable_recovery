/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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

#ifndef TWRP_FUNCTIONS_HPP
#define TWRP_FUNCTIONS_HPP

#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef BUILD_TWRPTAR_MAIN
#include "partitions.hpp"
#endif

namespace fs = std::filesystem;

#define CACHE_LOGS_DIR "/cache/" // For devices with a dedicated cache partition
#define DATA_LOGS_DIR "/data/"	 // For devices that do not have a dedicated cache partition

enum RebootCommand {
  CURRENT = 0,
  SYSTEM,
  RECOVERY,
  POWER_OFF,
  BOOTLOADER,
  DOWNLOAD,
  EDL,
  FSATBOOTD
};

enum ArchiveType : int {
  UNCOMPRESSED = 0,
  COMPRESSED,
  ENCRYPTED,
  COMPRESSED_ENCRYPTED
};

// Partition class
class TWFunc {
public:
  // Trims any trailing folders or filenames from the path, also adds a leading / if not present
  static std::string GetRootPath(const std::string& path);

  // Trims everything after the last / in the string
  static std::string GetPath(const std::string& path);

  // Trims the path off of a filename
  static std::string GetFilename(const std::string& path);

  // Executes cmd through the shell, appending its stdout to result; combine_stderr redirects
  // stderr into the same stream. Returns pclose()'s raw wait status (-1 on failure) — decode
  // with WIFEXITED/WEXITSTATUS (twrpRepacker.cpp relies on this).
  static int ExecCmd(const std::string& cmd, std::string& result, bool combine_stderr);

  // Executes cmd through the shell, displaying an error to the GUI if show_errors is true,
  // which is the default. Returns 0 on success, -1 on failure.
  static int ExecCmd(const std::string& cmd, bool show_errors = true);

  // Waits for pid to exit and checks exit status, displays an error to the GUI if show_errors is true which is the default
  static int WaitForChild(pid_t pid, int* status, const std::string& child_name, bool show_errors = true);

  // Waits for a pid to exit until the timeout is hit. If timeout is hit, kill the chilld.
  static int WaitForChildTimeout(pid_t pid, int* status, const std::string& child_name, int timeout);

  // Returns true if the path exists
  static bool IsPathExists(const std::string& path);

  // Classifies the first 2 bytes: COMPRESSED for gzip magic (0x1f 0x8b), ENCRYPTED for
  // OAES magic (0x4f 0x41), UNCOMPRESSED otherwise (including short or unopenable files)
  static ArchiveType GetFileType(const std::string& fn);

  // -1 for some error, 0 for failed to decrypt, 1 for decrypted, 3 for decrypted and found gzip format
  static int TryDecryptingFile(const std::string &fn, const std::string &password);

  // Encrypt stdin to stdout using the OAES chunked format
  static void AesEncryptStream(const std::string& password);

  // Decrypt stdin to stdout using the OAES chunked format
  static void AesDecryptStream(const std::string& password);

  // Returns the size of a file
  static unsigned long GetFileSize(const std::string& path);

  // Remove the beginning slash of a path
  static std::string RemoveBeginningSlash(const std::string& path);

  // Normalizes the path, e.g /data//media/ -> /data/media
  static std::string RemoveTrailingSlashes(const std::string& path, bool leave_last = false);

  // Removes at most one enclosing double quote from each end of str
  static std::string StripQuotes(const std::string& str);

  // Splits in at each occurrence of delimiter, dropping empty tokens when skip_empty is set
  static std::vector<std::string> SplitString(const std::string& in, char delimiter, bool skip_empty);

  // Returns end - start as a timespec with tv_nsec in [0, 1000000000) and a signed tv_sec
  static timespec TimespecDiff(const timespec& start, const timespec& end);

  // Returns the same end - start difference, truncated toward zero to whole milliseconds
  static int64_t TimespecDiffMs(const timespec& start, const timespec& end);

  // Wait For File, True is success, False is timeout;
  static bool WaitForFile(const std::string& path, std::chrono::nanoseconds timeout);

  // Kills processes using the given path so it can be unmounted
  static void KillForUseTargetProcess(const std::string& target);

#ifndef BUILD_TWRPTAR_MAIN
  // Recursively makes the entire path, creating each level with default metadata (ownership
  // and SELinux contexts). Returns false with a GUI error when a level cannot be created
  static bool RecursiveMkdir(const std::string& path);

  // Updates text for display in the GUI, e.g. Backing up %partition name%
  static void GuiOperationText(const std::string& read_value, const std::string& default_text);

  // Same as above but includes partition name
  static void GuiOperationText(const std::string& read_value, const std::string& partition_name, const std::string& default_text);

  // Writes the log to last_log
  static void UpdateLogFile();

  // Updates intent file
  static void UpdateIntentFile(const std::string& intent);

  static void SetInstallResult(int result);

  // Prepares the device for rebooting
  static int TwReboot(RebootCommand command);

  // checks for the existence of a script, chmods it to 755, then runs it
  static void CheckAndRunScript(const char* script_file, const char* display_name);

  // Recursively removes path, or only its contents when skip_parent is set. Returns -1 with a
  // GUI error when the path cannot be opened or a removal fails
  static int RemoveDir(const std::string& path, bool skip_parent);

  //copy file from src to dst with mode permissions
  static int CopyFile(const std::string& src, const std::string& dst, int mode, bool mount_paths = true);

  // Returns the dirent d_type (DT_*) of path via std::filesystem::status(), which
  // follows symlinks; DT_UNKNOWN if the path does not exist
  static unsigned int GetDTypeFromStat(const std::string& path);

  // Read file contents line by line
  static int ReadFile(const std::string& fn, std::vector<std::string>& results);

  // Read file contents with every newline removed
  static int ReadFile(const std::string& fn, std::string& results);

  // Read an unsigned integer from file (0 on parse failure, like stream extraction)
  static int ReadFile(const std::string& fn, uint64_t& results);

  // Write single line to file with no newline, truncating any existing file
  static bool WriteToFile(const std::string& fn, const std::string& line);

  // Append vector of strings line by line with newlines
  static bool WriteToFile(const std::string& fn, const std::vector<std::string>& lines);

  // Tries the password against every encrypted file in restore_path. Returns false (with the
  // stored restore password cleared) on the first file it fails to decrypt
  static bool TryDecryptingBackup(std::string restore_path, const std::string& password);

  // Returns the value of key from /system/build.prop (read via the Android root partition)
  static std::string GetPropertyFromSystem(const std::string& key);

  // Returns the value of prop_name from prop_file_name under mount_point, mounting that
  // partition if needed (unmounted again unless it was already mounted). Empty on failure.
  static std::string GetPropertyFromPartition(const std::string& prop_name,
                                              TWPartitionManager& partition_manager,
                                              const fs::path& mount_point,
                                              const std::string& prop_file_name = "build.prop");

  // Returns the current local date and time as yyyy-mm-dd--hh-mm-ss
  static std::string GetCurrentDate();

  // Populates TW_BACKUP_NAME with a backup name based on current date and ro.build.display.id from /system/build.prop
  static void AutoGenerateBackupName();

  // Fixes time on devices which need it (time_paths is a space separated list of paths to check for ats_* files)
  static void FixupTimeOnBoot(const std::string& time_paths = "");

  // Splits str at any character in delimiter (character-set semantics), dropping empty tokens
  // when remove_empty is set, which is the default
  static std::vector<std::string> SplitString(const std::string& str, const std::string& delimiter,
                                               bool remove_empty = true);

  // Create directory and it's parents, if they don't exist. mode, uid and gid are set to all _newly_ created folders. If whole path exists, do nothing.
  static bool CreateDirRecursive(const std::string& path, mode_t mode = 0755, uid_t uid = -1,
                                   gid_t gid = -1);

  // Well, you can read, it does what it says, passing return bool from TWFunc::WriteToFile ;)
  static int SetBrightness(const std::string& brightness_value);

  // Disables MTP if enable is false and re-enables MTP if enable is true and it was enabled the last time it was toggled off
  static bool ToggleMtp(bool enable);

  // support recovery.perf.mode
  static void SetPerformanceMode(bool mode);

  // Disable stock ROMs from replacing TWRP with stock recovery
  static void DisableStockRecoveryReplace();

  static uint64_t GetBlockSizeByIoctl(const char* block_device);

  // Copy Kernel Log to Current Storage (PSTORE/KMSG)
  static void CopyKernelLog(const std::string& curr_storage);

  // Copy Logcat to Current Storage
  static void CopyLogcat(const std::string& curr_storage);

  // return true if number, false if not a number
  static bool IsNumber(const std::string& str_to_check);

  // Tell ADB Backup to Stream to TWRP from GUI selection
  static int StreamAdbBackup(const std::string& restore_name);

  // return recovery log storage directory
  static std::string GetLogDir();

  // Loads /file_contexts into the global selinux_handle and reports whether the kernel
  // supports reading SELinux contexts
  static void CheckSelinuxSupport();

  // Override properties (including ro. properties)
  static int OverrideProperty(const std::string& key, const std::string& value);

  // Delete properties (non-persistent properties only)
  static int DeleteProperty(const std::string& key);

  // List current mounts by the kernel
  static void ListMounts();

  // Removes the bootloader message from misc for next boot
  static void ClearBootloaderMessage();

  // Gets user defined path on storage where backups should be stored
  static std::string CheckForTwrpFolder();

  // Returns true unless filename begins with the 4-byte ABX magic "ABX\0"; unopenable files
  // and files shorter than the magic count as plain XML
  static bool CheckXmlFormat(const std::string& filename);

  static bool FindFstab(std::string& fstab);

  // Looks up service's <hal> entry in the VINTF manifest under basepath's /etc/vintf, storing
  // its <version> value (or the version cut out of <fqname>) in res. Returns false when the
  // service is not found, leaving res untouched
  static bool GetServiceFromManifest(const std::string& basepath, const std::string& service,
                                     std::string& res);

  static std::string GetTwrpVersion();

  // Converts path from ABX to plain XML into a unique temp file under /tmp/abx2xml (or /tmp when
  // that directory cannot be created), storing the converted file's path in result. Returns false
  // with result untouched on failure
  static bool AbxToXml(const std::string& path, std::string& result);

private:
  // Gzips src onto dst via pigz, preserving any log contents already accumulated in dst
  static void CopyLog(const std::string& src, const std::string& dst);
};

extern int Log_Offset;
#else
};
#endif // ndef BUILD_TWRPTAR_MAIN

#endif // TWRP_FUNCTIONS_HPP
