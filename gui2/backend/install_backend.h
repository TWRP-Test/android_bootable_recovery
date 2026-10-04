#ifndef GUI2_BACKEND_INSTALL_BACKEND_H
#define GUI2_BACKEND_INSTALL_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

enum class install_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
};

struct install_status {
  install_state state = install_state::IDLE;
  // 0-100 as the zip reports it; -1 when it reports nothing.
  int progress = -1;
};

// A partition an image can be written to, as the legacy flash image page
// lists them.
struct image_target {
  std::string name;
  std::string mount_point;
  // A/B partitions get the legacy page's "flash to both slots" checkbox.
  bool slot_partition = false;
};

class install_backend {
 public:
  virtual ~install_backend() = default;

  // GUIAction::flash over the zip queue.
  virtual bool start_zip(const std::vector<std::string>& paths) = 0;

  virtual std::vector<image_target> image_targets() = 0;
  // The partition the legacy file selector picks for an image by its name;
  // empty when it picks none.
  virtual std::string image_target_for(const std::string& filename) = 0;
  virtual bool start_image(const std::string& path, const std::string& mount_point,
                           bool both_slots) = 0;

  virtual install_status status() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_INSTALL_BACKEND_H
