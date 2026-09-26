#ifndef GUI2_BACKEND_TOOLS_BACKEND_H
#define GUI2_BACKEND_TOOLS_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

// What the legacy getpartitiondetails action fills in for one partition.
struct partition_details {
  std::string name;
  std::string mount_point;
  std::string file_system;
  std::string size;
  std::string used;
  std::string free;
  std::string backup_size;
  bool present = false;
  bool removable = false;
  bool can_repair = false;
  bool can_resize = false;
  // The mkfs tools this build carries, as the values changefilesystem takes.
  std::vector<std::string> file_systems;
};

// The one-off jobs the legacy theme runs through confirm_action.
enum class tool_job {
  REPAIR,
  RESIZE,
  CHANGE_FILE_SYSTEM,
  RENAME_BACKUP,
  DELETE_BACKUP,
  TWRP_FOLDER,
  FIX_RECOVERY_BOOTLOOP,
  MERGE_SNAPSHOTS,
  DISABLE_AVB2,
};

enum class tool_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
};

// Which advanced rows the legacy theme would show on this device.
struct tool_availability {
  bool twrp_folder = false;
  bool fix_recovery_bootloop = false;
  bool merge_snapshots = false;
  bool disable_avb2 = false;
};

class tools_backend {
 public:
  virtual ~tools_backend() = default;

  virtual bool details(const std::string& mount_point, partition_details* out) = 0;
  virtual tool_availability availability() = 0;

  // tw_recovery_folder, e.g. "/TWRP".
  virtual std::string twrp_folder() = 0;
  // The current storage, e.g. "/sdcard"; the TWRP folder sits right under it.
  virtual std::string storage_path() = 0;
  virtual bool path_exists(const std::string& path) = 0;

  // The legacy multiuser warning: FBE with users still locked.
  virtual bool users_locked() = 0;

  // target is a mount point, a backup folder or empty; value is the new file
  // system or name.
  virtual bool start(tool_job job, const std::string& target, const std::string& value) = 0;
  virtual tool_state status() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TOOLS_BACKEND_H
