#ifndef GUI2_BACKEND_TWRP_MOUNT_BACKEND_H
#define GUI2_BACKEND_TWRP_MOUNT_BACKEND_H

#include <string>
#include <vector>

#include "mount_backend.h"

namespace gui2_backend {

class twrp_mount_backend final : public mount_backend {
 public:
  std::vector<mount_target> targets() override;
  bool set_mounted(const std::string& mount_point, bool mounted) override;
  bool system_writable() override;
  bool set_system_writable(bool writable) override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_MOUNT_BACKEND_H
