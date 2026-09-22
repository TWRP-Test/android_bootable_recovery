#include "twrp_file_manager_backend.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

#include "data.hpp"
#include "twrp-functions.hpp"

namespace gui2_backend {

namespace {

std::string quoted(const std::string& value) {
  // Single quotes stop the shell from touching anything but a quote itself.
  std::string out = "'";
  for (const char c : value) {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

std::string join(const std::string& directory, const std::string& name) {
  if (directory.empty() || directory == "/") return "/" + name;
  return directory + "/" + name;
}

}  // namespace

std::vector<file_entry> twrp_file_manager_backend::list(const std::string& path) {
  std::vector<file_entry> result;
  DIR* dir = opendir(path.c_str());
  if (dir == nullptr) return result;

  while (dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;

    file_entry item;
    item.name = name;
    struct stat info;
    if (lstat(join(path, name).c_str(), &info) == 0) {
      item.directory = S_ISDIR(info.st_mode);
      item.size = static_cast<uint64_t>(info.st_size);
      char mode[8];
      snprintf(mode, sizeof(mode), "%04o", info.st_mode & 07777);
      item.mode = mode;
    } else {
      // A broken symlink still deserves a row; it just has nothing to report.
      item.directory = entry->d_type == DT_DIR;
    }
    result.push_back(std::move(item));
  }
  closedir(dir);

  std::sort(result.begin(), result.end(), [](const file_entry& a, const file_entry& b) {
    if (a.directory != b.directory) return a.directory;
    return a.name < b.name;
  });
  return result;
}

bool twrp_file_manager_backend::remove(const std::string& path) {
  struct stat info;
  if (lstat(path.c_str(), &info) != 0) return false;
  if (S_ISDIR(info.st_mode)) return TWFunc::removeDir(path, false) == 0;
  return unlink(path.c_str()) == 0;
}

bool twrp_file_manager_backend::rename(const std::string& path, const std::string& name) {
  if (name.empty() || name.find('/') != std::string::npos) return false;
  const size_t slash = path.find_last_of('/');
  const std::string directory = slash == std::string::npos ? "/" : path.substr(0, slash);
  return ::rename(path.c_str(), join(directory, name).c_str()) == 0;
}

bool twrp_file_manager_backend::set_mode(const std::string& path, const std::string& mode) {
  return TWFunc::Exec_Cmd("chmod " + quoted(mode) + " " + quoted(path)) == 0;
}

// cp and mv rather than a hand-rolled copy: the legacy pages shell out too, and
// these have to handle directories, symlinks and permissions.
bool twrp_file_manager_backend::copy(const std::string& path, const std::string& destination) {
  return TWFunc::Exec_Cmd("cp -a " + quoted(path) + " " + quoted(destination) + "/") == 0;
}

bool twrp_file_manager_backend::move(const std::string& path, const std::string& destination) {
  return TWFunc::Exec_Cmd("mv " + quoted(path) + " " + quoted(destination) + "/") == 0;
}

std::string twrp_file_manager_backend::start_directory() {
  const std::string storage = DataManager::GetCurrentStoragePath();
  return storage.empty() ? "/" : storage;
}

}  // namespace gui2_backend
