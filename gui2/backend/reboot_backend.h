#ifndef GUI2_BACKEND_REBOOT_BACKEND_H
#define GUI2_BACKEND_REBOOT_BACKEND_H

#include <string>

namespace gui2_backend {

enum class reboot_target {
  SYSTEM,
  POWER_OFF,
  RECOVERY,
  FASTBOOT,
  BOOTLOADER,
  DOWNLOAD,
  EDL,
};

enum class boot_slot {
  A,
  B,
};

struct reboot_capabilities {
  bool system = false;
  bool power_off = false;
  bool recovery = false;
  bool fastboot = false;
  bool bootloader = false;
  bool download = false;
  bool edl = false;
  bool boot_slots = false;
};

class reboot_backend {
 public:
  virtual ~reboot_backend() = default;

  virtual const reboot_capabilities& capabilities() const = 0;
  virtual std::string active_slot() const = 0;
  virtual bool set_active_slot(boot_slot slot) = 0;
  virtual bool request_reboot(reboot_target target) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_REBOOT_BACKEND_H
