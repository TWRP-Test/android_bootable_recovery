#ifndef GUI2_BACKEND_TWRP_REBOOT_BACKEND_H
#define GUI2_BACKEND_TWRP_REBOOT_BACKEND_H

#include "reboot_backend.h"

namespace gui2_backend {

class twrp_reboot_backend final : public reboot_backend {
 public:
  twrp_reboot_backend();

  const reboot_capabilities& capabilities() const override;
  std::string active_slot() const override;
  bool set_active_slot(boot_slot slot) override;
  bool request_reboot(reboot_target target) override;

 private:
  bool is_supported(reboot_target target) const;
  const char* reboot_argument(reboot_target target) const;

  reboot_capabilities capabilities_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_REBOOT_BACKEND_H
