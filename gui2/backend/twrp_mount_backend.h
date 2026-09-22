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

  std::vector<storage_device> storages() override;
  bool select_storage(const std::string& path) override;
  std::string storage_name() override;
  std::string storage_free() override;

  bool mtp_enabled() override;
  bool set_mtp_enabled(bool enabled) override;

  bool has_usb_storage() override;
  bool usb_storage_enabled() override;
  bool set_usb_storage_enabled(bool enabled) override;

 private:
  // Nothing in the recovery keeps this, so the page asks us instead.
  bool usb_storage_on_ = false;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_MOUNT_BACKEND_H
