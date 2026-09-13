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

#include "twrp_functions.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fs_mgr_priv.h>
#include <android-base/chrono_utils.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <selinux/label.h>

#include "abx.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "gui/gui.hpp"
#include "oaes/oaes.hpp"
#include "twinstall/install.h"

#ifndef BUILD_TWRPTAR_MAIN
#include <cutils/android_reboot.h>
#include <cutils/properties.h>
#include <sys/reboot.h>

#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"
#include "bootloader_message/bootloader_message.h"
#include "gui/pages.hpp"
#include "gui/rapidxml.hpp"
#endif // ndef BUILD_TWRPTAR_MAIN

#ifdef TW_INCLUDE_LIBRESETPROP
#include <resetprop.hpp>
#endif

selabel_handle* selinux_handle;

std::string TWFunc::GetTwrpVersion() {
  const std::string dev = android::base::GetProperty("ro.twrp.device_version", "");
  return !dev.empty()
           ? std::format("{}-{}", TW_MAIN_VERSION_STR, dev)
           : std::string{ TW_VERSION_STR };
}

/* Execute a command */
int TWFunc::ExecCmd(const std::string& cmd, std::string& result, const bool combine_stderr) {
  const std::string popen_cmd = combine_stderr ? cmd + " 2>&1" : cmd;

  // The deleter funnels pclose()'s wait status into `status` — unique_ptr's
  // destructor would otherwise discard it.
  int status = -1;
  const auto reap = [&status](FILE* pipe_fd) {
    status = pclose(pipe_fd);
  };
  std::unique_ptr<FILE, decltype(reap)> stream(popen(popen_cmd.c_str(), "r"), reap);
  if (!stream) {
    LOGERR("ExecCmd(): failed to execute command: %s\n", cmd.c_str());
    return -1;
  }

  // fread by count rather than fgets: NUL bytes and a missing final newline
  // in the command's output reach `result` untouched.
  char buffer[4096];
  size_t bytes;
  while ((bytes = fread(buffer, 1, sizeof(buffer), stream.get())) > 0) {
    result.append(buffer, bytes);
  }
  stream.reset(); // pcloses the stream and reaps the wait status
  return status;
}

int TWFunc::ExecCmd(const std::string& cmd, const bool show_errors) {
  int status = 0;
  switch (const pid_t pid = fork()) {
    case -1:
      LOGERR("ExecCmd(): fork failed: %d!\n", errno);
      return -1;
    case 0: // child
      execl("/system/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
      _exit(127); // execl only returns on failure
    default:
      return WaitForChild(pid, &status, cmd, show_errors) != 0 ? -1 : 0;
  }
}

// Returns "file.name" from a full /path/to/file.name
std::string TWFunc::GetFilename(const std::string& path) {
  return fs::path(path).filename();
}

// Returns "/path/to/" from a full /path/to/file.name
std::string TWFunc::GetPath(const std::string& path) {
  if (const size_t pos = path.find_last_of('/'); pos != std::string::npos) {
    return path.substr(0, pos + 1);
  }
  return path;
}

int TWFunc::WaitForChild(const pid_t pid, int* status, const std::string& child_name, const bool show_errors) {
  if (const pid_t rc_pid = waitpid(pid, status, 0); rc_pid > 0) {
    if (WIFSIGNALED(*status)) {
      if (show_errors) gui_msg(
          Msg(msg::kError, "pid_signal={1} process ended with signal: {2}")(child_name)(
              WTERMSIG(*status))); // Seg fault or some other non-graceful termination
      return -1;
    }
    if (WEXITSTATUS(*status) == 0) {
      LOGINFO("%s process ended with RC=%d\n", child_name.c_str(), WEXITSTATUS(*status)); // Success
    } else {
      if (show_errors) gui_msg(
          Msg(msg::kError, "pid_error={1} process ended with ERROR: {2}")(child_name)(
              WEXITSTATUS(*status))); // Graceful exit, but there was an error
      return -1;
    }
  } else {
    // no PID returned
    if (errno == ECHILD)
      LOGERR("%s no child process exist\n", child_name.c_str());
    else {
      LOGERR("%s Unexpected error %d\n", child_name.c_str(), errno);
      return -1;
    }
  }
  return 0;
}

int TWFunc::WaitForChildTimeout(const pid_t pid, int* status, const std::string& child_name,
                                int timeout) {
  pid_t ret_pid = waitpid(pid, status, WNOHANG);
  for (; ret_pid == 0 && timeout; --timeout) {
    sleep(1);
    ret_pid = waitpid(pid, status, WNOHANG);
  }
  if (ret_pid == 0 && timeout == 0) {
    LOGERR("%s took too long, killing process\n", child_name.c_str());
    kill(pid, SIGKILL);
    for (timeout = 5; ret_pid == 0 && timeout; --timeout) {
      sleep(1);
      ret_pid = waitpid(pid, status, WNOHANG);
    }
    if (ret_pid)
      LOGINFO("Child process killed successfully\n");
    else
      LOGINFO("Child process took too long to kill, may be a zombie process\n");
    return -1;
  }
  if (ret_pid > 0) {
    if (WIFSIGNALED(*status)) {
      gui_msg(
          Msg(msg::kError, "pid_signal={1} process ended with signal: {2}")(child_name)(
              WTERMSIG(*status))); // Seg fault or some other non-graceful termination
      return -1;
    }
  } else if (ret_pid < 0) {
    // no PID returned
    if (errno == ECHILD)
      LOGERR("%s no child process exist\n", child_name.c_str());
    else {
      LOGERR("%s Unexpected error %d\n", child_name.c_str(), errno);
      return -1;
    }
  }
  return 0;
}

bool TWFunc::IsPathExists(const std::string& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

void TWFunc::KillForUseTargetProcess(const std::string& target) {
  if (!target.empty()) {
    char cmd_buf[256] = { 0 };
    snprintf(
        cmd_buf,
        sizeof(cmd_buf),
        "lsof | awk '{print($1,$9,$2)}' | grep -v '^recovery ' | awk '{print($2,$3)}' | grep '^%s' | awk '{print $2}' | sort | uniq | xargs kill -9 > /dev/null 2>&1",
        target.c_str()
        );
    std::string ret;
    ExecCmd(cmd_buf, ret, false);
  }
}

ArchiveType TWFunc::GetFileType(const std::string& fn) {
  const android::base::unique_fd fd(open(fn.c_str(), O_RDONLY | O_CLOEXEC));
  if (fd == -1) return UNCOMPRESSED;
  std::array<unsigned char, 2> magic{};
  // Short or unopenable files fail the read and count as UNCOMPRESSED; the former
  // behavior read an uninitialized header buffer in that case
  if (!android::base::ReadFully(fd, magic.data(), magic.size())) return UNCOMPRESSED;
  if (magic[0] == 0x1f && magic[1] == 0x8b) return COMPRESSED;
  if (magic[0] == 0x4f && magic[1] == 0x41) return ENCRYPTED;
  return UNCOMPRESSED; // default
}

int TWFunc::TryDecryptingFile(const std::string& fn, const std::string& password) {
  return Oaes::TryDecryptingFile(fn, password);
}

void TWFunc::AesEncryptStream(const std::string& password) {
  Oaes::EncryptStream(password);
}

void TWFunc::AesDecryptStream(const std::string& password) {
  Oaes::DecryptStream(password);
}

unsigned long TWFunc::GetFileSize(const std::string& path) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  return ec ? 0 : static_cast<unsigned long>(size);
}

std::string TWFunc::RemoveBeginningSlash(const std::string& path) {
  // Strip only a leading slash. The former find_first_of('/') cut at the first slash
  // anywhere, silently dropping everything before it on relative paths.
  if (!path.empty() && path.front() == '/') {
    return path.substr(1);
  }
  return path;
}

std::string TWFunc::RemoveTrailingSlashes(const std::string& path, const bool leave_last) {
  // Re-joining the components collapses runs of separators and drops the trailing one
  // (which iterates as a final empty component); "." and ".." pass through unchanged.
  fs::path joined;
  for (const fs::path& component : fs::path(path)) {
    if (!component.empty()) {
      joined /= component;
    }
  }
  std::string res = joined.string();
  if (res.find_first_not_of('/') == std::string::npos) {
    res.clear(); // a separators-only path reduces to the empty string
  }
  if (leave_last) {
    res += '/';
  }
  return res;
}

std::string TWFunc::StripQuotes(const std::string& str) {
  std::string_view view = str;
  if (view.starts_with('"')) {
    view.remove_prefix(1);
  }
  if (view.ends_with('"')) {
    view.remove_suffix(1);
  }
  return std::string(view);
}

// Tokenizer shared by the TWFunc::SplitString overloads: splits input at any single character
// in delimiters (character-set semantics) and drops empty tokens when skip_empty is set. After
// a match the scan advances by delimiters.size(), matching the historical string overload.
static std::vector<std::string> SplitByCharset(const std::string_view input, const std::string_view delimiters,
                                               const bool skip_empty) {
  std::vector<std::string> tokens;
  size_t idx = 0, idx_last = 0;

  while (idx < input.size()) {
    idx = input.find_first_of(delimiters, idx_last);
    if (idx == std::string_view::npos) idx = input.size();

    if (idx - idx_last != 0 || !skip_empty)
      tokens.emplace_back(input.substr(idx_last, idx - idx_last));

    idx_last = idx + delimiters.size();
    // A multi-character delimiter set can advance past the end; the old string overload threw
    // std::out_of_range there, so stop instead of reading out of bounds.
    if (idx_last > input.size()) break;
  }
  return tokens;
}

std::vector<std::string> TWFunc::SplitString(const std::string& in, const char delimiter,
                                             const bool skip_empty) {
  // A NUL delimiter has always produced no tokens.
  if (delimiter == '\0') return {};

  return SplitByCharset(in, std::string_view(&delimiter, 1), skip_empty);
}

// Common duration core of the TimespecDiff pair: end - start as a nanoseconds count.
static std::chrono::nanoseconds DurationBetween(const timespec& start, const timespec& end) {
  const auto to_duration = [](const timespec& ts) {
    return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
  };
  return to_duration(end) - to_duration(start);
}

timespec TWFunc::TimespecDiff(const timespec& start, const timespec& end) {
  const std::chrono::nanoseconds elapsed = DurationBetween(start, end);
  // The floor decomposition reproduces the historical borrow arithmetic: tv_nsec stays in
  // [0, 1000000000) with a signed tv_sec, also for a negative difference.
  const auto whole = std::chrono::floor<std::chrono::seconds>(elapsed);
  timespec diff{};
  diff.tv_sec = whole.count();
  diff.tv_nsec = (elapsed - whole).count();
  return diff;
}

int64_t TWFunc::TimespecDiffMs(const timespec& start, const timespec& end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(DurationBetween(start, end)).count();
}

bool TWFunc::WaitForFile(const std::string& path, const std::chrono::nanoseconds timeout) {
  const android::base::Timer t;
  while (t.duration() < timeout) {
    struct stat sb{};
    if (stat(path.c_str(), &sb) != -1) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

#ifndef BUILD_TWRPTAR_MAIN

// Returns "/path" from a full /path/to/file.name
std::string TWFunc::GetRootPath(const std::string& path) {
  std::string local_path = path;

  // Make sure that we have a leading slash
  if (!local_path.starts_with('/')) local_path = "/" + local_path;

  // Trim the path to get the root path only: keep the leading slash plus the first component
  if (const size_t position = local_path.find('/', 2); position != std::string::npos) {
    local_path.resize(position);
  }
  return local_path;
}

bool TWFunc::RecursiveMkdir(const std::string& path) {
  fs::path cur;
  for (const auto& part : fs::path(path)) {
    cur /= part;
    std::error_code ec;
    if (fs::exists(cur, ec)) continue;
    fs::create_directory(cur, ec);
    if (ec) {
      gui_msg(Msg(msg::kError, "create_folder_strerr=Can not create '{1}' folder ({2}).")(
          cur.string())(ec.message()));
      return false;
    }
    tw_set_default_metadata(cur.c_str());
  }
  return true;
}

void TWFunc::GuiOperationText(const std::string& read_value, const std::string& default_text) {
  std::string display_text;

  DataManager::GetValue(read_value, display_text);
  if (display_text.empty()) display_text = default_text;

  DataManager::SetValue("tw_operation", display_text);
  DataManager::SetValue("tw_partition", "");
}

void TWFunc::GuiOperationText(const std::string& read_value, const std::string& partition_name,
                              const std::string& default_text) {
  std::string display_text;

  DataManager::GetValue(read_value, display_text);
  if (display_text.empty()) display_text = default_text;

  DataManager::SetValue("tw_operation", display_text);
  DataManager::SetValue("tw_partition", partition_name);
}

void TWFunc::CopyLog(const std::string& src, const std::string& dst) {
  PartitionManager.Mount_By_Path(dst, false);

  // Recover the previous log contents: decompress the old .gz if present, otherwise read the
  // matching uncompressed file and remove it (it gets folded into the new .gz).
  const std::string uncompressed_log = dst.substr(0, dst.find(".gz"));
  std::string dest_log_buffer;
  if (IsPathExists(dst)) {
    if (GetFileType(dst) == COMPRESSED) {
      // ExecCmd appends its output to dest_log_buffer
      if (ExecCmd("pigz -c -d " + dst, dest_log_buffer, false) < 0) {
        LOGINFO("Unable to get destination logfile contents.\n");
        return;
      }
    }
  } else if (IsPathExists(uncompressed_log)) {
    android::base::ReadFileToString(uncompressed_log, &dest_log_buffer);
    unlink(uncompressed_log.c_str());
  }

  std::string src_log_buffer;
  android::base::ReadFileToString(src, &src_log_buffer);

  // Pipe the old + new contents through pigz, which writes the compressed result to dst.
  int log_pipe[2];
  if (pipe(log_pipe) < 0) {
    LOGINFO("Unable to open pipe to write to persistent log file: %s\n", dst.c_str());
    return;
  }
  android::base::unique_fd pipe_read(log_pipe[0]);
  android::base::unique_fd pipe_write(log_pipe[1]);
  const android::base::unique_fd destination_fd(open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666));

  const pid_t pigz_pid = fork();
  if (pigz_pid < 0) {
    LOGINFO("fork() failed\n");
    return;  // all descriptors close through their unique_fd owners
  }
  if (pigz_pid == 0) {
    // child: gzip standard input into the destination file
    pipe_write.reset();
    dup2(pipe_read.get(), STDIN_FILENO);
    dup2(destination_fd.get(), STDOUT_FILENO);
    execlp("pigz", "pigz", "-", nullptr);
    _exit(-1);  // execlp only returns on failure
  }

  // parent: closing the write end gives pigz EOF; the unique_fd owners close everything else
  pipe_read.reset();
  if (write(pipe_write.get(), dest_log_buffer.data(), dest_log_buffer.size()) < 0 ||
      write(pipe_write.get(), src_log_buffer.data(), src_log_buffer.size()) < 0) {
    LOGINFO("Unable to append to persistent log: %s\n", dst.c_str());
  }
  pipe_write.reset();
  waitpid(pigz_pid, nullptr, 0);  // reap pigz instead of leaving a zombie behind
}

void TWFunc::UpdateLogFile() {
  const fs::path log_dir = GetLogDir();
  const fs::path recovery_dir = log_dir / "recovery";

  if (log_dir == CACHE_LOGS_DIR) {
    if (!PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, false)) {
      LOGINFO("Failed to mount %s for TWFunc::UpdateLogFile\n", CACHE_LOGS_DIR);
    }
  }

  if (!IsPathExists(recovery_dir)) {
    LOGINFO("Recreating %s folder.\n", recovery_dir.c_str());
    if (!CreateDirRecursive(recovery_dir, S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP, 0, 0)) {
      LOGINFO("Unable to create %s folder.\n", recovery_dir.c_str());
    }
  }

  const fs::path log_copy = recovery_dir / "log.gz";
  const fs::path last_log_copy = recovery_dir / "last_log.gz";
  CopyFile(log_copy, last_log_copy, 0600);
  CopyLog(TMP_LOG_FILE, log_copy);
  chown(log_copy.c_str(), 1000, 1000);
  chmod(log_copy.c_str(), 0600);
  chmod(last_log_copy.c_str(), 0640);

  if (log_dir == CACHE_LOGS_DIR) {
    if (PartitionManager.Mount_By_Path("/cache", false)) {
      if (unlink("/cache/recovery/command") && errno != ENOENT) {
        LOGINFO("Can't unlink %s\n", "/cache/recovery/command");
      }
    }
  }
  sync();
}

void TWFunc::ClearBootloaderMessage() {
  std::string err;
  if (!clear_bootloader_message(&err)) {
    LOGINFO("%s\n", err.c_str());
  }
}

void TWFunc::UpdateIntentFile(const std::string& intent) {
  if (PartitionManager.Mount_By_Path("/cache", false) && !intent.empty()) {
    WriteToFile("/cache/recovery/intent", intent);
  }
#if defined(TW_CACHE_INTENT_COLOROS) || AB_OTA_UPDATER
  if (PartitionManager.Mount_By_Path(DATA_LOGS_DIR, false) && !intent.empty()) {
    WriteToFile("/data/cache/recovery/intent", intent);
  }
#endif
}

static int install_result = INSTALL_NONE;

void TWFunc::SetInstallResult(const int result) {
  install_result = result;
}

// reboot: Reboot the system. Return -1 on error, no return on success
int TWFunc::TwReboot(const RebootCommand command) {
  DataManager::Flush();
  UpdateLogFile();

#ifdef TW_CACHE_INTENT_COLOROS
  if (install_result == INSTALL_SUCCESS) {
    // "0" -> OTA_UPDATE_OK
    // "1" -> OTA_UPDATE_FAILED
    // "2" -> RECOVERY_UPDATE_OK
    // "3" -> RECOVERY_UPDATE_FAILED
    UpdateIntentFile("2");
  }
#endif

  // Always force a sync before we reboot
  sync();

  if (TWPartition* data_part = PartitionManager.Find_Partition_By_Path("/data")) {
    if (data_part->Is_Mounted()) {
      data_part->UnMount(false);
    }
  }

  switch (command) {
    case CURRENT:
    case SYSTEM:
#ifndef TW_CACHE_INTENT_COLOROS
      UpdateIntentFile("s");
#endif
      sync();
      CheckAndRunScript("/system/bin/rebootsystem.sh", "reboot system");
#ifdef ANDROID_RB_PROPERTY
      return property_set(ANDROID_RB_PROPERTY, "reboot,");
#elif defined(ANDROID_RB_RESTART)
      return android_reboot(ANDROID_RB_RESTART, 0, 0);
#else
      return reboot(RB_AUTOBOOT);
#endif
    case RECOVERY:
      CheckAndRunScript("/system/bin/rebootrecovery.sh", "reboot recovery");
      return android::base::SetProperty(ANDROID_RB_PROPERTY, "reboot,recovery");
    case BOOTLOADER:
      CheckAndRunScript("/system/bin/rebootbootloader.sh", "reboot bootloader");
      return android::base::SetProperty(ANDROID_RB_PROPERTY, "reboot,bootloader");
    case POWER_OFF:
      CheckAndRunScript("/system/bin/poweroff.sh", "power off");
#ifdef ANDROID_RB_PROPERTY
      return property_set(ANDROID_RB_PROPERTY, "shutdown,");
#elif defined(ANDROID_RB_POWEROFF)
      return android_reboot(ANDROID_RB_POWEROFF, 0, 0);
#else
      return reboot(RB_POWER_OFF);
#endif
    case DOWNLOAD:
      CheckAndRunScript("/system/bin/rebootdownload.sh", "reboot download");
      return android::base::SetProperty(ANDROID_RB_PROPERTY, "reboot,download");
    case EDL:
      CheckAndRunScript("/system/bin/rebootedl.sh", "reboot edl");
      return android::base::SetProperty(ANDROID_RB_PROPERTY, "reboot,edl");
    case FSATBOOTD:
      return android::base::SetProperty(ANDROID_RB_PROPERTY, "reboot,fastboot");
    default:
      return -1;
  }
  return -1;
}

void TWFunc::CheckAndRunScript(const char* script_file, const char* display_name) {
  // Check for and run startup script if script exists
  if (struct stat st{}; stat(script_file, &st) == 0) {
    gui_msg(Msg("run_script=Running {1} script...")(display_name));
    chmod(script_file, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    ExecCmd(script_file);
    gui_msg("done=Done.");
  }
}

int TWFunc::RemoveDir(const std::string& path, const bool skip_parent) {
  // The directory_iterator construction doubles as the opendir() probe of the old code: it
  // fails for a missing path or lacking permissions, without removing anything.
  std::error_code ec;
  const fs::directory_iterator entries(path, ec);
  if (ec) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(path)(ec.message()));
    return -1;
  }

  if (skip_parent) {
    // Keep path itself, remove everything below it
    for (const fs::directory_entry& entry : entries) {
      fs::remove_all(entry, ec);
    }
  } else {
    fs::remove_all(path, ec);
  }
  return ec ? -1 : 0;
}

int TWFunc::CopyFile(const std::string& src, const std::string& dst, int mode, bool mount_paths) {
  if (mount_paths) {
    PartitionManager.Mount_By_Path(src, false);
    PartitionManager.Mount_By_Path(dst, false);
  }
  if (!IsPathExists(src)) {
    LOGINFO("Path %s does not exist. Unable to copy file to %s\n", src.c_str(), dst.c_str());
    return -1;
  }
  std::error_code ec;
  fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    LOGINFO("Unable to copy file %s to %s\n", src.c_str(), dst.c_str());
    return -1;
  }
  if (chmod(dst.c_str(), mode) != 0) {
    LOGERR("Unable to chmod file: %s. Error: %s\n", dst.c_str(), strerror(errno));
    return -1;
  }
  return 0;
}

unsigned int TWFunc::GetDTypeFromStat(const std::string& path) {
  // fs::status() follows symlinks like stat(), so file_type::symlink (and DT_LNK) is
  // unreachable; not_found/none cover a missing path, matching the old stat() failure.
  switch (std::error_code ec; fs::status(path, ec).type()) {
    case fs::file_type::directory:
      return DT_DIR;
    case fs::file_type::block:
      return DT_BLK;
    case fs::file_type::character:
      return DT_CHR;
    case fs::file_type::fifo:
      return DT_FIFO;
    case fs::file_type::regular:
      return DT_REG;
    case fs::file_type::socket:
      return DT_SOCK;
    default:
      return DT_UNKNOWN;
  }
}

int TWFunc::ReadFile(const std::string& fn, std::string& results) {
  std::ifstream file(fn);
  if (!file.is_open()) {
    LOGINFO("Cannot find file %s\n", fn.c_str());
    return -1;
  }
  // Historic getline-concatenation contract: return the file content with
  // every newline removed (digest and sysfs callers compare against
  // newline-free strings).
  results.assign(std::istreambuf_iterator(file), std::istreambuf_iterator<char>());
  std::erase(results, '\n');
  return 0;
}

int TWFunc::ReadFile(const std::string& fn, std::vector<std::string>& results) {
  std::ifstream file(fn);
  if (!file.is_open()) {
    LOGINFO("Cannot find file %s\n", fn.c_str());
    return -1;
  }
  std::string line;
  while (std::getline(file, line)) results.push_back(line);
  return 0;
}

int TWFunc::ReadFile(const std::string& fn, uint64_t& results) {
  std::ifstream file(fn);
  if (!file.is_open()) {
    LOGINFO("Cannot find file %s\n", fn.c_str());
    return -1;
  }
  std::string line;
  std::getline(file, line);
  // Reset first: from_chars leaves the value untouched on failure where the
  // old `file >> results` stored 0. Partial-parsing also tolerates sysfs
  // trailing characters, like istream extraction did.
  results = 0;
  std::from_chars(line.data(), line.data() + line.size(), results);
  return 0;
}

bool TWFunc::WriteToFile(const std::string& fn, const std::string& line) {
  std::ofstream file(fn, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    LOGINFO("Cannot find file %s\n", fn.c_str());
    return false;
  }
  file.write(line.data(), static_cast<std::streamsize>(line.size()));
  file.close(); // flush; sets failbit on write or close errors
  return !file.fail();
}

bool TWFunc::WriteToFile(const std::string& fn, const std::vector<std::string>& lines) {
  std::ofstream file(fn, std::ios::out | std::ios::app);
  if (!file.is_open()) {
    LOGINFO("Cannot find file %s\n", fn.c_str());
    return false;
  }
  for (const auto& line : lines) file << line << '\n';
  file.close(); // flush; sets failbit on write or close errors
  return !file.fail();
}


bool TWFunc::TryDecryptingBackup(std::string restore_path, const std::string& password) {
  restore_path += '/';
  // directory_iterator skips "." and ".." and reports an unopenable path through ec,
  // replacing the old opendir() probe
  std::error_code ec;
  const fs::directory_iterator entries(restore_path, ec);
  if (ec) {
    gui_msg(
        Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(restore_path)(
            ec.message()));
    return false;
  }

  // TryDecryptingFile returns -1/0 on failure, 1 or 3 on success; only 3 (decrypted and gzip)
  // passes here. all_of short-circuits at the first file that fails, and an empty directory
  // counts as decrypted, like the loop it replaces.
  const bool all_decrypted = std::ranges::all_of(entries, [&](const fs::directory_entry& entry) {
    return GetFileType(entry.path()) != ENCRYPTED || TryDecryptingFile(entry.path(), password) >= 2;
  });
  if (!all_decrypted) {
    DataManager::SetValue("tw_restore_password", "");  // Clear the bad password
    DataManager::SetValue("tw_restore_display", "");   // Also clear the display mask
  }
  return all_decrypted;
}

std::string TWFunc::GetCurrentDate() {
  constexpr auto seconds = std::time_t{};
  std::tm local{};
  localtime_r(&seconds, &local);  // thread-safe, unlike localtime()
  return std::format("{:04d}-{:02d}-{:02d}--{:02d}-{:02d}-{:02d}", local.tm_year + 1900,
                     local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

std::string TWFunc::GetPropertyFromSystem(const std::string& key) {
  return GetPropertyFromPartition(key, PartitionManager, PartitionManager.Get_Android_Root_Path());
}

std::string TWFunc::GetPropertyFromPartition(const std::string& prop_name,
                                             TWPartitionManager& partition_manager,
                                             const fs::path& mount_point,
                                             const std::string& prop_file_name) {
  const bool was_mounted = partition_manager.Is_Mounted_By_Path(mount_point);
  if (!partition_manager.Mount_By_Path(mount_point, true)) {
    return {};
  }

  const std::string prop_file = mount_point == partition_manager.Get_Android_Root_Path()
                                  ? mount_point / "system" / prop_file_name
                                  : mount_point / prop_file_name;

  std::string prop_value;
  if (!IsPathExists(prop_file)) {
    LOGINFO("Unable to locate file: %s\n", prop_file.c_str());
  } else if (std::vector<std::string> prop_lines; ReadFile(prop_file, prop_lines) != 0) {
    LOGINFO("Unable to open %s for getting '%s'.\n", prop_file_name.c_str(), prop_name.c_str());
    // Keep a date-based backup name available when the prop file is unreadable.
    DataManager::SetValue(TW_BACKUP_NAME, GetCurrentDate());
  } else {
    // First matching line wins; a line without '=' matches only a key equal to the whole
    // line (npos + 1 wraps to 0, so its value is then the whole line too).
    for (const std::string& line : prop_lines) {
      if (const size_t separator = line.find('=');
        std::string_view(line).substr(0, separator) == prop_name) {
        prop_value = line.substr(separator + 1);
        break;
      }
    }
  }

  if (!was_mounted) {
    partition_manager.UnMount_By_Path(mount_point, false);
  }
  return prop_value;
}

void TWFunc::AutoGenerateBackupName() {
  std::string prop_value = GetPropertyFromSystem("ro.build.display.id");
  if (prop_value.empty()) {
    DataManager::SetValue(TW_BACKUP_NAME, GetCurrentDate());
    return;
  }

  // remove periods from build display so it doesn't confuse the extension code
  std::erase(prop_value, '.');
  std::string backup_name = std::format("{}_{}", GetCurrentDate(), prop_value);
  if (backup_name.size() > MAX_BACKUP_NAME_LEN) backup_name.resize(MAX_BACKUP_NAME_LEN);

  // Trailing spaces cause problems on some file systems, so remove them
  const std::string space = " ";
  std::string space_check = backup_name.substr(backup_name.size() - 1, 1);
  while (space_check == space) {
    backup_name.resize(backup_name.size() - 1);
    space_check = backup_name.substr(backup_name.size() - 1, 1);
  }
  std::ranges::replace(backup_name, ' ', '_');
  if (PartitionManager.Check_Backup_Name(backup_name, false, true) != 0) {
    LOGINFO("Auto generated backup name '%s' is not valid, using date instead.\n",
            backup_name.c_str());
    DataManager::SetValue(TW_BACKUP_NAME, GetCurrentDate());
  } else {
    DataManager::SetValue(TW_BACKUP_NAME, backup_name);
  }
}

void TWFunc::FixupTimeOnBoot(const std::string& time_paths /* = "" */) {
#ifdef QCOM_RTC_FIX
  static bool fixed = false;
  if (fixed) return;

  LOGINFO("TWFunc::FixupTimeOnBoot: Pre-fix date and time: %s\n",
          TWFunc::GetCurrentDate().c_str());

  timeval tv{};
  uint64_t offset = 0;
  const std::string since_epoch = "/sys/class/rtc/rtc0/since_epoch";

  // Stage 1: try reading the raw RTC counter
  if (ReadFile(since_epoch, offset) == 0) {
    LOGINFO("TWFunc::FixupTimeOnBoot: Setting time offset from file %s\n", since_epoch.c_str());
    tv.tv_sec = static_cast<long>(offset);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    if (std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) > 1517600000) {
      // Anything older than 2 Feb 2018 19:33:20 GMT will do nicely thank you ;)
      LOGINFO("TWFunc::FixupTimeOnBoot: Date and time corrected: %s\n",
              TWFunc::GetCurrentDate().c_str());
      fixed = true;
      return;
    }
  } else {
    LOGINFO("TWFunc::FixupTimeOnBoot: opening %s failed\n", since_epoch.c_str());
  }

  LOGINFO("TWFunc::FixupTimeOnBoot: will attempt to use the ats files now.\n");

  // Devices with Qualcomm Snapdragon 800 do some shenanigans with RTC.
  // They never set it, it just ticks forward from 1970-01-01 00:00,
  // and then they have files /data/system/time/ats_* with 64bit offset
  // in milliseconds which, when added to the RTC, gives the correct time.
  // So, the time is: (offset_from_ats + value_from_RTC)
  // There are multiple ats files, they are for different systems? Bases?
  // Like, ats_1 is for modem and ats_2 is for TOD (time of day?).
  // Look at file time_genoff.h in CodeAurora, qcom-opensource/time-services

  std::vector<std::string> paths; // space separated list of paths
  if (time_paths.empty()) {
    paths = {"/data/system/time/", "/data/time/", "/data/vendor/time/"};
    if (!PartitionManager.Mount_By_Path("/data", false)) return;
  } else {
    // When specific path(s) are used, FixupTimeOnBoot needs those
    // partitions to already be mounted!
    paths = SplitString(time_paths, " ");
  }

  offset = 0;
  std::string ats_path;

  // Prefer ats_2, it seems to be the one we want according to logcat on hammerhead
  // - it is the one for ATS_TOD (time of day?).
  // However, I never saw a device where the offset differs between ats files.
  // symlink_status (lstat) matches the old dt->d_type == DT_REG exactly:
  // symlinks are excluded, unlike is_regular_file() which would follow them.
  for (const auto& path : paths) {
    for (std::error_code ec; const fs::directory_entry& entry : fs::directory_iterator(path, ec)) {
      const std::string name = entry.path().filename().string();
      if (!fs::is_regular_file(fs::symlink_status(entry.path())) ||
          !name.starts_with("ats_"))
        continue;
      if (ats_path.empty() || name == "ats_2")
        ats_path = (fs::path(path) / name).string();
    }
  }

  if (ats_path.empty()) {
    LOGINFO("TWFunc::FixupTimeOnBoot: no ats files found, leaving untouched!\n");
  } else {
    if (std::ifstream f(ats_path, std::ios::binary); !f.is_open()) {
      LOGINFO("TWFunc::FixupTimeOnBoot: failed to open file %s\n", ats_path.c_str());
    } else if (!f.read(reinterpret_cast<char*>(&offset), sizeof(offset))) {
      LOGINFO("TWFunc::FixupTimeOnBoot: failed load uint64 from file %s\n", ats_path.c_str());
    } else {
      LOGINFO("TWFunc::FixupTimeOnBoot: Setting time offset from file %s, offset %lu\n",
              ats_path.c_str(), offset);
      DataManager::SetValue("tw_qcom_ats_offset", offset, 1);
      fixed = true;
    }
  }

  if (!fixed) {
#ifdef TW_QCOM_ATS_OFFSET
    // Offset is the difference between the current time and the time since_epoch
    // To calculate the offset in Android, the following expression (from a root shell) can be used:
    // echo "$(( ($(date +%s) - $(cat /sys/class/rtc/rtc0/since_epoch)) ))"
    // Add 3 zeros to the output and use that in the TW_QCOM_ATS_OFFSET flag in your BoardConfig.mk
    // For example, if the result of the calculation is 1642433544, use 1642433544000 as the offset
    offset = static_cast<uint64_t>(TW_QCOM_ATS_OFFSET);
    DataManager::SetValue("tw_qcom_ats_offset", offset, 1);
    LOGINFO("TWFunc::FixupTimeOnBoot: Setting time offset from TW_QCOM_ATS_OFFSET, offset %lu\n",
            offset);
#else
    // Failed to get offset from ats file, check twrp settings
    uint64_t value;
    if (DataManager::GetValue("tw_qcom_ats_offset", value) < 0) {
      return;
    }
    offset = value;
    LOGINFO("TWFunc::FixupTimeOnBoot: Setting time offset from twrp setting file, offset %lu\n",
            offset);
    // Do not consider the settings file as a definitive answer, keep fixed=false so next run will try ats files again
#endif
  }

  // Apply offset to system clock via chrono arithmetic — replaces the old gettimeofday +
  // manual usec while-normalisation loop
  namespace chrono = std::chrono;
  const auto corrected = chrono::system_clock::now() + chrono::milliseconds(offset);
  tv.tv_sec = chrono::system_clock::to_time_t(corrected);
  tv.tv_usec = chrono::duration_cast<chrono::microseconds>(
      corrected.time_since_epoch() % chrono::seconds(1)).count();
#ifdef TW_CLOCK_OFFSET
  // Some devices are even quirkier and have ats files that are offset from the actual time
  tv.tv_sec = tv.tv_sec + TW_CLOCK_OFFSET;
#endif
  settimeofday(&tv, nullptr);

  LOGINFO("TWFunc::FixupTimeOnBoot: Date and time corrected: %s\n",
          TWFunc::GetCurrentDate().c_str());
#endif
}

std::vector<std::string> TWFunc::SplitString(const std::string& str, const std::string& delimiter,
                                             const bool remove_empty) {
  return SplitByCharset(str, delimiter, remove_empty);
}

bool TWFunc::CreateDirRecursive(const std::string& path, const mode_t mode, const uid_t uid,
                                const gid_t gid) {
  std::string cur_path;
  struct stat info{};
  for (const auto& part : SplitString(path, "/")) {
    cur_path += "/" + part;
    if (stat(cur_path.c_str(), &info) < 0 || !S_ISDIR(info.st_mode)) {
      if (mkdir(cur_path.c_str(), mode) < 0) return false;
      chown(cur_path.c_str(), uid, gid);
    }
  }
  return true;
}

int TWFunc::SetBrightness(const std::string& brightness_value) {
  int result = -1;

  if (DataManager::GetIntValue("tw_has_brightnesss_file")) {
    std::string secondary_brightness_file;
    LOGINFO("TWFunc::SetBrightness: Setting brightness control to %s\n", brightness_value.c_str());
    result = WriteToFile(DataManager::GetStrValue("tw_brightness_file"), brightness_value);
    DataManager::GetValue("tw_secondary_brightness_file", secondary_brightness_file);
    if (!secondary_brightness_file.empty()) {
      LOGINFO("TWFunc::SetBrightness: Setting secondary brightness control to %s\n",
              brightness_value.c_str());
      WriteToFile(secondary_brightness_file, brightness_value);
    }
  }
  return result ? 0 : -1;
}

bool TWFunc::ToggleMtp(bool enable) {
#ifdef TW_HAS_MTP
  static bool was_enabled = false;

  if (enable && was_enabled) {
    if (!PartitionManager.Enable_MTP()) PartitionManager.Disable_MTP();
  } else {
    was_enabled = DataManager::GetIntValue("tw_mtp_enabled");
    PartitionManager.Disable_MTP();
    usleep(500);
  }
  return was_enabled;
#else
  return false;
#endif
}

void TWFunc::SetPerformanceMode(const bool mode) {
  android::base::SetProperty("recovery.perf.mode", mode ? "1" : "0");
  // Some time for events to catch up to init handlers
  usleep(500000);
}

void TWFunc::DisableStockRecoveryReplace() {
  if (PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), false)) {
    // Disable flashing of stock recovery
    if (IsPathExists("/system/recovery-from-boot.p")) {
      rename("/system/recovery-from-boot.p", "/system/recovery-from-boot.bak");
      gui_msg(
          "rename_stock=Renamed stock recovery file in /system to prevent the stock ROM from replacing TWRP.");
      sync();
    }
    PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
  }
}

uint64_t TWFunc::GetBlockSizeByIoctl(const char* block_device) {
  if (const int fd = open(block_device, O_RDONLY); fd < 0) {
    LOGINFO("IoctlGetBlockSize: Failed to open '%s', (%s)\n", block_device, strerror(errno));
  } else {
    int ret = 0;
    uint64_t block_device_size;
    ret = ioctl(fd, BLKGETSIZE, &block_device_size);
    close(fd);
    if (ret) {
      LOGINFO("IoctlGetBlockSize: ioctl error: (%s)\n", strerror(errno));
    } else {
      return block_device_size * 512;
    }
  }
  return 0;
}

void TWFunc::CopyKernelLog(const std::string& curr_storage) {
  const std::string dmesg_dst = fs::path(curr_storage) / "dmesg.log";

  // klogctl(2) reads the kernel ring buffer directly, where the old "dmesg" exec was a
  // no-op whenever /system was not mounted yet. KLOG_SIZE_BUFFER sizes the buffer so
  // KLOG_READ_ALL is not truncated; the ring may still grow between the two calls, like dmesg
  // itself. Recovery runs as root, so dmesg_restrict does not apply.
  std::string result;
  if (const int buf_size = klogctl(KLOG_SIZE_BUFFER, nullptr, 0); buf_size > 0) {
    std::vector<char> buf(buf_size);
    if (const int n = klogctl(KLOG_READ_ALL, buf.data(), buf_size); n > 0) {
      result.assign(buf.data(), n);
    }
  }
  WriteToFile(dmesg_dst, result);
  gui_msg(Msg("copy_kernel_log=Copied kernel log to {1}")(dmesg_dst));
  tw_set_default_metadata(dmesg_dst.c_str());
}

void TWFunc::CopyLogcat(const std::string& curr_storage) {
  const std::string logcat_dst = fs::path(curr_storage) / "logcat.txt";
  const std::string logcat_cmd = "logcat -d";

  std::string result;
  ExecCmd(logcat_cmd, result, false);
  WriteToFile(logcat_dst, result);
  gui_msg(Msg("copy_logcat=Copied logcat to {1}")(logcat_dst));
  tw_set_default_metadata(logcat_dst.c_str());
}

bool TWFunc::IsNumber(const std::string& str_to_check) {
  int value = 0;
  const std::string_view s = str_to_check;
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  return ec == std::errc{} && end == s.data() + s.size();  // full-consumption, no trailing characters
}

int TWFunc::StreamAdbBackup(const std::string& restore_name) {
  const std::string cmd = "/system/bin/bu --twrp stream " + restore_name;
  LOGINFO("StreamAdbBackup: %s\n", cmd.c_str());
  const int ret = ExecCmd(cmd);
  if (ret != 0) return -1;
  return ret;
}

std::string TWFunc::GetLogDir() {
  if (!PartitionManager.Find_Partition_By_Path(CACHE_LOGS_DIR)) {
    if (!PartitionManager.Find_Partition_By_Path(DATA_LOGS_DIR)) {
      LOGINFO("Unable to find a directory to store TWRP logs.");
      return "";
    }
    return DATA_LOGS_DIR;
  }
  return CACHE_LOGS_DIR;
}

void TWFunc::CheckSelinuxSupport() {
  // Prefer the file_contexts shipped with the recovery image over an existing one
  if (IsPathExists("/prebuilt_file_contexts")) {
    std::error_code ec;
    if (IsPathExists("/file_contexts")) {
      LOGINFO("Renaming regular /file_contexts -> /file_contexts.bak\n");
      fs::rename("/file_contexts", "/file_contexts.bak", ec);
    }
    LOGINFO("Moving /prebuilt_file_contexts -> /file_contexts\n");
    fs::rename("/prebuilt_file_contexts", "/file_contexts", ec);
    if (ec) {
      LOGINFO("Renaming file contexts failed: %s\n", ec.message().c_str());
    }
  }

  constexpr selinux_opt selinux_options[] = {
      {.type = SELABEL_OPT_PATH, .value = "/file_contexts"},
  };
  selinux_handle = selabel_open(SELABEL_CTX_FILE, selinux_options, 1);
  if (!selinux_handle) {
    LOGINFO("No file contexts for SELinux\n");
  } else {
    LOGINFO("SELinux contexts loaded from /file_contexts\n");
  }

  // Check to ensure SELinux can be supported by the kernel. When the probe directory is
  // missing the check defaults to reporting support, like the historical code did.
  const std::string cache_dir = GetLogDir();
  if (cache_dir == CACHE_LOGS_DIR) {
    PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, false);
  }
  const std::string se_context_check = cache_dir + "recovery/";
  char* contexts = nullptr;
  int ret = 0;
  if (IsPathExists(se_context_check)) {
    ret = lgetfilecon(se_context_check.c_str(), &contexts);
    if (ret < 0) {
      LOGINFO("Could not check %s SELinux contexts.\n", se_context_check.c_str());
    }
  }
  if (ret < 0) {
    gui_warn("no_kernel_selinux=Kernel does not have support for reading SELinux contexts.");
  } else {
    freecon(contexts);  // a no-op for nullptr, the probe-not-run case
    gui_msg("full_selinux=Full SELinux support is present.");
  }
}

int TWFunc::OverrideProperty(const std::string& key, const std::string& value) {
#ifdef TW_INCLUDE_LIBRESETPROP
  return setprop(key.c_str(), value.c_str(), false);
#else
  return NOT_AVAILABLE;
#endif
}

int TWFunc::DeleteProperty(const std::string& key) {
#ifdef TW_INCLUDE_LIBRESETPROP
  return delprop(key.c_str(), false);
#else
  return NOT_AVAILABLE;
#endif
}

void TWFunc::ListMounts() {
  std::vector<std::string> mounts;
  ReadFile("/proc/mounts", mounts);
  LOGINFO("Mounts:\n");
  for (auto&& mount : mounts) {
    LOGINFO("%s\n", mount.c_str());
  }
}

std::string TWFunc::CheckForTwrpFolder() {
  const fs::path main_path = DataManager::GetCurrentStoragePath();
  // TW_DEFAULT_RECOVERY_FOLDER is "/TWRP" (leading slash); .filename() yields the bare
  // "TWRP" so we can join via operator/ below without the absolute-replace trap.
  const std::string default_folder = fs::path(TW_DEFAULT_RECOVERY_FOLDER).filename();

  if (DataManager::GetIntValue(TW_IS_ENCRYPTED) && DataManager::GetIntValue(TW_CRYPTO_PWTYPE)) {
    return TW_DEFAULT_RECOVERY_FOLDER;
  }

  // Scan the storage root for directories marked with a .twrpcf file.
  // directory_iterator skips "." and ".." natively, and directory_entry::is_directory
  // subsumes the former DT_UNKNOWN/GetDTypeFromStat fallback (and stats each entry's
  // own path, fixing the backup loop's prior main_path-based stat).
  std::error_code ec;
  fs::directory_iterator entries(main_path, ec);
  if (ec) return TW_DEFAULT_RECOVERY_FOLDER;

  std::string old_folder;
  std::vector<std::string> customTwrpFolders;
  for (const auto& entry : entries) {
    if (!entry.is_directory(ec)) continue;
    if (!fs::exists(entry.path() / ".twrpcf", ec)) continue;

    if (std::string name = entry.path().filename(); name == default_folder) {
      old_folder = name;
    } else {
      customTwrpFolders.push_back(name);
    }
  }

  if (old_folder.empty() && customTwrpFolders.empty()) {
    LOGINFO("No recovery folder found. Using default folder.\n");
    return TW_DEFAULT_RECOVERY_FOLDER;
  }
  if (customTwrpFolders.empty()) {
    LOGINFO("No custom recovery folder found. Using TWRP as default.\n");
    return TW_DEFAULT_RECOVERY_FOLDER;
  }

  if (customTwrpFolders.size() > 1) {
    LOGINFO("More than one custom recovery folder found. Using first one from the list.\n");
  } else {
    LOGINFO("One custom recovery folder found.\n");
  }

  // custom_path is a "/name" fragment returned to callers; gui/gui.cpp concatenates it as a
  // string and depends on the leading slash, so the return contract keeps it. Internally we
  // join the bare chosen name via operator/ (no leading slash → no absolute-replace trap).
  const std::string& chosen = customTwrpFolders.front();
  const std::string custom_path = '/' + chosen;

  // Migrate backups from the default folder into the chosen custom one, then remove default.
  if (const fs::path default_folder_path = main_path / default_folder; fs::exists(default_folder_path, ec)) {
    const std::string device_id = DataManager::GetStrValue("device_id");
    const fs::path old_backup_folder = default_folder_path / "BACKUPS" / device_id;
    const fs::path new_backup_folder = main_path / chosen / "BACKUPS" / device_id;

    if (fs::exists(old_backup_folder, ec)) {
      std::vector<std::string> backups;
      for (fs::directory_iterator backup_entries(old_backup_folder, ec);
           const auto& backup : backup_entries) {
        if (backup.is_directory(ec)) {
          backups.push_back(backup.path().filename());
        }
      }

      // Replicate `mv -f src dst`: when dst is an existing directory, mv merges src
      // INTO it (→ dst/src.filename()); fs::rename would instead fail, so the is_dir
      // case renames into dst. The _new suffix avoids clobbering a same-named backup.
      for (const auto& name : backups) {
        const fs::path src = old_backup_folder / name;
        const fs::path base = new_backup_folder / name;
        if (const fs::path dst = fs::exists(base, ec)
                                   ? new_backup_folder / (name + "_new")
                                   : base; fs::is_directory(dst, ec)) {
          fs::rename(src, dst / name, ec);
        } else {
          fs::rename(src, dst, ec);
        }
      }
    }
    fs::remove_all(default_folder_path, ec);
  }

  return custom_path;
}

bool TWFunc::CheckXmlFormat(const std::string& filename) {
  // Android Binary XML starts with these bytes; the explicit length keeps the NUL, which a
  // plain literal would lose to strlen
  constexpr std::string_view kAbxMagic{"ABX\0", 4};

  std::string header(4, '\0');
  std::ifstream file(filename, std::ios::binary);
  // unopenable counts as plain, like the is_open() guard it replaces
  if (!file.is_open()) return true;
  file.read(header.data(), static_cast<uint32_t>(header.size())); // short files keep their zero fill

  return !header.starts_with(kAbxMagic); // ABX format - requires conversion
}

// return true=successful conversion (return the name of the converted file in "result");
// return false=an error happened (leave "result" alone)
bool TWFunc::AbxToXml(const std::string& path, std::string& result) {
  if (!IsPathExists(path)) return false;

  fs::path dir = "/tmp/abx2xml";
  if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) dir = "/tmp";

  // mkstemp replaces the trailing X's in place, so the template has to live in a std::string
  // passed through non-const data() for the unique name to survive the call
  std::string tmp_path = (dir / "abxXXXXXX").string();
  android::base::unique_fd fd(mkstemp(tmp_path.data()));
  if (!fd.ok()) {
    LOGINFO("Error. The abx conversion of %s has failed (mkstemp errno %d).\n",
            path.c_str(), errno);
    return false;
  }
  fd.reset(); // abx2xml() reopens the path itself

  if (abx2xml(path, tmp_path, /*in_place=*/false) != 0 ||
      !IsPathExists(tmp_path)) {
    LOGINFO("Error. The abx conversion of %s has failed.\n", path.c_str());
    std::error_code ec;
    fs::remove(tmp_path, ec);
    return false;
  }
  result = tmp_path;
  return true;
}

static std::string GetFstabPath() {
  for (const char* prop : { "fstab_suffix", "hardware", "hardware.platform" }) {
    std::string suffix;

    if (!fs_mgr_get_boot_config(prop, &suffix)) continue;

    for (const char* prefix : {
           // late-boot/post-boot locations
           "/odm/etc/fstab.", "/vendor/etc/fstab.",
           // early boot locations
           "/system/etc/fstab.", "/first_stage_ramdisk/system/etc/fstab.",
           "/fstab.", "/first_stage_ramdisk/fstab." }) {
      std::string fstab_path = prefix + suffix;
      LOGINFO("%s: %s\n", __func__, fstab_path.c_str());
      if (access(fstab_path.c_str(), F_OK) == 0) return fstab_path;
    }
  }

  return "";
}

bool TWFunc::FindFstab(std::string& fstab) {
  fstab = GetFstabPath();
  return !fstab.empty();
}

// Extracts the version from a fully-qualified interface name, e.g. the "4.0" in
// android.hardware.keymaster@4.0::IKeymasterDevice
static std::string GetVersionFromFq(const std::string& name) {
  // find('@') + 1 wraps to 0 when there is no '@', like the int math it replaces
  const size_t start = name.find('@') + 1;
  return name.substr(start, name.find(':', start) - start);
}

bool TWFunc::GetServiceFromManifest(const std::string& basepath, const std::string& service, std::string& res) {
  const fs::path manifest_path = fs::path(basepath) / "etc/vintf";
  bool ret = false;

  // Prefer using ro.boot.product.vendor.sku property, following AOSP VintfObject::fetchVendorHalManifest
  // If not set, also try ro.board.platform.
  std::string platform = android::base::GetProperty("ro.boot.product.vendor.sku", "");
  if (platform.empty()) {
    LOGINFO(
        "Property ro.boot.product.vendor.sku not found, trying to get vintf manifest file name from ro.board.platform\n");
    platform = android::base::GetProperty("ro.board.platform", "");
  }

  // Let's find the service xml if exists. The in-process walk replaces shelling out to
  // "find <manifest_path>manifest/ -type f -name *<service>*": find recursed into subdirectories
  // (so does the recursive iterator) and matched basenames against the *service* glob, which for
  // the dotted interface names used here is a plain substring test. A missing or unusable
  // manifest/ directory leaves filename empty, like find's empty output did.
  std::string filename;
  for (std::error_code walk_ec; const fs::directory_entry& entry :
       fs::recursive_directory_iterator(manifest_path / "manifest", walk_ec)) {
    if (std::error_code type_ec; entry.is_regular_file(type_ec) &&
                                 entry.path().filename().native().find(service) != std::string::npos) {
      filename = entry.path();
      break; // the shell-out used only the first line of find's output
    }
  }
  if (filename.empty()) {
    LOGINFO("Separate manifest doesn't exist for '%s'\n", service.c_str());
    // Look for manifest_PLATFORM.xml
    filename = manifest_path / std::format("manifest_{}.xml", platform);
    if (!IsPathExists(filename)) {
      // Use legacy manifest path if platform manifest is not found.
      LOGINFO("%s not found. Using default path for manifest.xml\n", filename.c_str());
      filename = manifest_path / "manifest.xml";
    }
  }
  if (!IsPathExists(filename)) return ret;

  const std::unique_ptr<char, decltype(&free)> manifest(
      PageManager::LoadFileToBuffer(filename, nullptr), free);
  if (!manifest) return ret;
  LOGINFO("Looking for '%s' service in manifest\n", service.c_str());

  // rapidxml points its nodes into manifest's buffer, so the document must not outlive it
  rapidxml::xml_document<> vintf_manifest;
  vintf_manifest.parse<0>(manifest.get());
  if (const rapidxml::xml_node<>* manifest_node = vintf_manifest.first_node("manifest")) {
    for (const rapidxml::xml_node<>* hal = manifest_node->first_node(); hal;
         hal = hal->next_sibling()) {
      if (std::string_view(hal->name()) != "hal") continue;
      const rapidxml::xml_node<>* name_node = hal->first_node("name");
      if (name_node == nullptr) continue;
      if (std::string_view(name_node->value()) != service) continue;

      const rapidxml::xml_node<>* version_node = hal->first_node("version");
      if (version_node != nullptr) {
        LOGINFO("Found version in manifest: %s\n", version_node->value());
      } else {
        version_node = hal->first_node("fqname");
        if (version_node == nullptr) return ret;
        LOGINFO("Found fqname in manifest: %s\n", version_node->value());
      }
      if (const std::string_view version = version_node->value();
          version.find('@') == std::string_view::npos) {
        res = version;
      } else {
        res = GetVersionFromFq(version_node->value());
      }
      ret = true;
    }
  }
  return ret;
}

#endif // ndef BUILD_TWRPTAR_MAIN