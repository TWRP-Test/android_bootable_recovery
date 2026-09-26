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
  std::string detail;
  // Set when a zip asked for it; the page offers the wipe afterwards.
  bool cache_wipe_requested = false;
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

  // Legacy queues zips and flashes them in order; one at a time is the same
  // thing with a queue of one.
  virtual bool start_zip(const std::vector<std::string>& paths, bool verify_digest) = 0;

  virtual std::vector<image_target> image_targets() = 0;
  virtual bool start_image(const std::string& path, const std::string& mount_point,
                           bool both_slots) = 0;

  virtual install_status status() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_INSTALL_BACKEND_H
