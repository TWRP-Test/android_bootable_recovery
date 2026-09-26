#include "twrp_hardware_settings.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "data.hpp"
#include "settings_store.h"
#include "twrp_functions.hpp"
#include "twrpminui/minui.h"

namespace gui2_backend {
namespace {

constexpr int kMinBrightnessPercent = 10;
constexpr int kMaxBrightnessPercent = 100;

// On some panels a backlight write is a panel command that blocks for several
// milliseconds, longer than a frame at 120 Hz. A drag hands the value to this
// thread, which only ever writes the newest one.
class backlight_writer {
 public:
  backlight_writer() { std::thread(&backlight_writer::run, this).detach(); }

  void set(int value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_ = value;
    }
    wake_.notify_one();
  }

 private:
  void run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      wake_.wait(lock, [this] { return pending_ >= 0; });
      const int value = pending_;
      pending_ = -1;
      lock.unlock();
      TWFunc::SetBrightness(std::to_string(value));
      lock.lock();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  int pending_ = -1;
};

// Never destroyed: the detached thread waits on it until the process ends.
backlight_writer& backlight() {
  static backlight_writer* writer = new backlight_writer();
  return *writer;
}

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

twrp_hardware_settings::twrp_hardware_settings(settings_store* settings) : settings_(settings) {}

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
  backlight().set(value);

  return settings_->set_persistent("tw_brightness", std::to_string(value)) &&
         settings_->set_persistent("tw_brightness_pct", std::to_string(percent));
}

// The legacy settings page offers the vibration tab on tw_disable_haptics.
bool twrp_hardware_settings::has_haptics() const {
  return DataManager::GetIntValue("tw_disable_haptics") == 0;
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
