#ifndef GUI2_BACKEND_FILE_MANAGER_BACKEND_H
#define GUI2_BACKEND_FILE_MANAGER_BACKEND_H

#include <cstdint>
#include <string>
#include <vector>

namespace gui2_backend {

struct file_entry {
  std::string name;
  bool directory = false;
  uint64_t size = 0;
  // Permission bits as the chmod dialog shows them, e.g. "0755".
  std::string mode;
};

// The legacy file manager's actions, minus the ones that only make sense with
// its own confirmation pages.
class file_manager_backend {
 public:
  virtual ~file_manager_backend() = default;

  // Directories first, then files, both by name. Unreadable directories come
  // back empty rather than as an error: recovery mounts come and go.
  virtual std::vector<file_entry> list(const std::string& path) = 0;

  virtual bool remove(const std::string& path) = 0;
  virtual bool rename(const std::string& path, const std::string& name) = 0;
  virtual bool set_mode(const std::string& path, const std::string& mode) = 0;
  // Both take a destination directory, the way the legacy pages do.
  virtual bool copy(const std::string& path, const std::string& destination) = 0;
  virtual bool move(const std::string& path, const std::string& destination) = 0;

  // Where to start. The current storage, or / when there is none.
  virtual std::string start_directory() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_FILE_MANAGER_BACKEND_H
