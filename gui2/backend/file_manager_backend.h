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
  // Seconds since the epoch, for sorting by date.
  int64_t modified = 0;
};

// A legacy <fileselector>'s <filter>: which files it lists besides folders.
struct file_filter {
  // ';' separated endings, matched as written; empty lists every file.
  std::string extn;
  // ';' separated beginnings a file may match instead.
  std::string prfx;
};

// GUIFileSelector::GetFileList: folders and files, each sorted by
// tw_gui_sort_order. Not opened: the legacy list steps up to the parent.
struct file_listing {
  std::vector<file_entry> folders;
  std::vector<file_entry> files;
  bool opened = true;
};

class file_manager_backend {
 public:
  virtual ~file_manager_backend() = default;

  virtual file_listing list(const std::string& folder, const file_filter& filter) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_FILE_MANAGER_BACKEND_H
