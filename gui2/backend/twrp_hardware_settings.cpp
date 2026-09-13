#include "twrp_hardware_settings.h"

#include <algorithm>
#include <string>

#include "data.hpp"
#include "settings_store.h"
#include "twrp-functions.hpp"
#include "twrpminui/minui.h"

namespace gui2_backend {
namespace {

constexpr int kMinBrightnessPercent = 10;
constexpr int kMaxBrightnessPercent = 100;

const char* haptic_key(haptic_channel channel) {
  switch (channel) {
    case haptic_channel::BUTTON:
      return "tw_button_vibrate";
    case haptic_channel::KEYBOARD:
      return "tw_keyboard_vibrate";
    case haptic_channel::ACTION:
      return "tw_action_vibrate";
  }
  return "tw_button_vibrate";
}

int haptic_default(haptic_channel channel) {
  switch (channel) {
    case haptic_channel::BUTTON:
      return 80;
    case haptic_channel::KEYBOARD:
      return 40;
    case haptic_channel::ACTION:
      return 160;
  }
  return 80;
}

}  // namespace

twrp_hardware_settings::twrp_hardware_settings(settings_store* settings)
    : settings_(settings),
#ifdef TW_NO_HAPTICS
      haptics_available_(false) {
}
#else
      haptics_available_(haptics_available() != 0) {
}
#endif

bool twrp_hardware_settings::has_brightness() const {
  return DataManager::GetIntValue("tw_has_brightnesss_file") != 0 &&
         DataManager::GetIntValue("tw_brightness_max") > 0;
}

int twrp_hardware_settings::brightness_percent() const {
  if (!has_brightness()) return 0;
  const int percent = settings_ == nullptr ? DataManager::GetIntValue("tw_brightness_pct")
                                           : settings_->get_int("tw_brightness_pct", 20);
  return std::clamp(percent, kMinBrightnessPercent, kMaxBrightnessPercent);
}

bool twrp_hardware_settings::set_brightness_percent(int percent) {
  if (!has_brightness() || settings_ == nullptr) return false;

  percent = std::clamp(percent, kMinBrightnessPercent, kMaxBrightnessPercent);
  const int maximum = DataManager::GetIntValue("tw_brightness_max");
  const int value = maximum * percent / 100;
  if (TWFunc::Set_Brightness(std::to_string(value)) != 0) return false;

  return settings_->set_persistent("tw_brightness", std::to_string(value)) &&
         settings_->set_persistent("tw_brightness_pct", std::to_string(percent));
}

bool twrp_hardware_settings::has_haptics() const {
  return haptics_available_;
}

int twrp_hardware_settings::haptic_duration_ms(haptic_channel channel) const {
  const int maximum = channel == haptic_channel::ACTION ? 500 : 300;
  const int value = settings_ == nullptr
                        ? DataManager::GetIntValue(haptic_key(channel))
                        : settings_->get_int(haptic_key(channel), haptic_default(channel));
  return std::clamp(value, 0, maximum);
}

bool twrp_hardware_settings::set_haptic_duration_ms(haptic_channel channel, int duration_ms) {
  if (!has_haptics() || settings_ == nullptr) return false;
  const int maximum = channel == haptic_channel::ACTION ? 500 : 300;
  duration_ms = std::clamp(duration_ms, 0, maximum);
  return settings_->set_persistent(haptic_key(channel), std::to_string(duration_ms));
}

void twrp_hardware_settings::vibrate(haptic_channel channel) {
  if (!has_haptics()) return;
  DataManager::Vibrate(haptic_key(channel));
}

}  // namespace gui2_backend
