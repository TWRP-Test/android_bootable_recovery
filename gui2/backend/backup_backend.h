#ifndef GUI2_BACKEND_BACKUP_BACKEND_H
#define GUI2_BACKEND_BACKUP_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

struct backup_target {
  std::string name;   // already carries the size the legacy list shows
  std::string mount_point;
};

enum class backup_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
  CANCELLED,
};

struct backup_status {
  backup_state state = backup_state::IDLE;
  std::string detail;  // bytes/files line published by the running job
};

class backup_backend {
 public:
  virtual ~backup_backend() = default;

  virtual std::vector<backup_target> targets() = 0;

  // An empty name lets TWRP generate one from the current date.
  virtual bool start(const std::vector<std::string>& mount_points, const std::string& name,
                     bool compress, bool skip_digest, bool encrypt,
                     const std::string& password) = 0;
  virtual void cancel() = 0;
  virtual backup_status status() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_BACKUP_BACKEND_H
