#ifndef GUI2_BACKEND_MOUNT_BACKEND_H
#define GUI2_BACKEND_MOUNT_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

struct mount_target {
  std::string name;
  std::string mount_point;
  bool mounted = false;
};

class mount_backend {
 public:
  virtual ~mount_backend() = default;

  virtual std::vector<mount_target> targets() = 0;

  // Mount and unmount are quick enough to run on the UI thread; both report
  // whether the partition ended up in the requested state.
  virtual bool set_mounted(const std::string& mount_point, bool mounted) = 0;

  // System is remounted read-only after every boot unless the user asks
  // otherwise, which the legacy UI exposes as its own toggle.
  virtual bool system_writable() = 0;
  virtual bool set_system_writable(bool writable) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_MOUNT_BACKEND_H
