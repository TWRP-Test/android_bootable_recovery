#ifndef GUI2_BACKEND_RESTORE_BACKEND_H
#define GUI2_BACKEND_RESTORE_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

// One backup folder on the current storage.
struct restore_backup {
  std::string name;    // the folder, which is the date TWRP named it after
  std::string path;    // full path, which is what the restore runs against
  std::string detail;  // what the list shows under the name
};

// A partition the chosen backup actually holds.
struct restore_target {
  std::string name;
  std::string mount_point;
};

enum class restore_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
};

struct restore_status {
  restore_state state = restore_state::IDLE;
  std::string detail;
};

// Restoring works in two steps, the way the legacy UI does it: pick a folder,
// then pick what to take out of it. Opening a folder is what reveals which
// partitions it holds and whether it is encrypted.
class restore_backend {
 public:
  virtual ~restore_backend() = default;

  virtual std::vector<restore_backup> backups() = 0;

  // Reads the folder. Everything below describes whichever folder was opened
  // last.
  virtual bool open(const std::string& path) = 0;
  virtual std::vector<restore_target> targets() = 0;
  virtual bool encrypted() = 0;
  virtual std::string date() = 0;

  // An encrypted backup is opened before it is restored, exactly as the legacy
  // flow does it: every encrypted file in the folder is tried, and a wrong
  // password stops there rather than part way through a restore.
  virtual bool unlock(const std::string& password) = 0;

  // check_digest mirrors tw_skip_digest_check, which despite its name means
  // "verify" when it is set.
  virtual bool start(const std::vector<std::string>& mount_points, bool check_digest) = 0;
  virtual restore_status status() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_RESTORE_BACKEND_H
