#ifndef GUI2_BACKEND_HARDWARE_SETTINGS_H
#define GUI2_BACKEND_HARDWARE_SETTINGS_H

namespace gui2_backend {

enum class haptic_channel {
  BUTTON,
  KEYBOARD,
  ACTION,
};

class hardware_settings {
 public:
  virtual ~hardware_settings() = default;

  virtual bool has_brightness() const = 0;
  virtual int brightness_percent() const = 0;
  virtual bool set_brightness_percent(int percent) = 0;

  virtual bool has_haptics() const = 0;
  virtual int haptic_duration_ms(haptic_channel channel) const = 0;
  virtual bool set_haptic_duration_ms(haptic_channel channel, int duration_ms) = 0;
  virtual void vibrate(haptic_channel channel) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_HARDWARE_SETTINGS_H
