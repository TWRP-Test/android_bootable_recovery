#ifndef GUI2_BACKEND_TWRP_HARDWARE_SETTINGS_H
#define GUI2_BACKEND_TWRP_HARDWARE_SETTINGS_H

#include "hardware_settings.h"

namespace gui2_backend {

class settings_store;

class twrp_hardware_settings final : public hardware_settings {
 public:
  explicit twrp_hardware_settings(settings_store* settings);

  bool has_brightness() const override;
  int brightness_percent() const override;
  bool set_brightness_percent(int percent) override;

  bool has_haptics() const override;
  int haptic_duration_ms(haptic_channel channel) const override;
  bool set_haptic_duration_ms(haptic_channel channel, int duration_ms) override;
  void vibrate(haptic_channel channel) override;

 private:
  settings_store* settings_;
  bool haptics_available_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_HARDWARE_SETTINGS_H
