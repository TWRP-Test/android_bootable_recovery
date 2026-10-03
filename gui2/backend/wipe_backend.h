#ifndef GUI2_BACKEND_WIPE_BACKEND_H
#define GUI2_BACKEND_WIPE_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

struct wipe_target {
  std::string name;
  std::string mount_point;
};

enum class wipe_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
};

struct wipe_status {
  wipe_state state = wipe_state::IDLE;
  int done = 0;
  int total = 0;
};

// Long running partition operations. The start calls return once the work is
// queued; progress is read back through status().
class wipe_backend {
 public:
  virtual ~wipe_backend() = default;

  virtual std::vector<wipe_target> targets() = 0;
  virtual bool has_data_media() = 0;

  virtual bool start_factory_reset() = 0;
  virtual bool start_format_data() = 0;
  virtual bool start_wipe(const std::vector<std::string>& mount_points) = 0;

  virtual wipe_status status() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_WIPE_BACKEND_H
