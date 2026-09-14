#include "status_backend.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <utility>

#include "recovery_utils/battery_utils.h"
#include "settings_store.h"
#include "twcommon.h"

namespace gui2_backend {
namespace {

std::string format_time(bool military_time) {
  const time_t now = time(nullptr);
  struct tm local_time;
  if (localtime_r(&now, &local_time) == nullptr) return "--:--";

  char output[32];
  if (military_time) {
    std::snprintf(output, sizeof(output), "%d:%02d", local_time.tm_hour, local_time.tm_min);
  } else {
    const int hour = local_time.tm_hour == 0
                         ? 12
                         : (local_time.tm_hour > 12 ? local_time.tm_hour - 12 : local_time.tm_hour);
    std::snprintf(output, sizeof(output), "%d:%02d %s", hour, local_time.tm_min,
                  local_time.tm_hour >= 12 ? "PM" : "AM");
  }
  return output;
}

#ifdef TW_USE_LEGACY_BATTERY_SERVICES
std::string battery_file(const char* name) {
#ifdef TW_CUSTOM_BATTERY_PATH
  return std::string(EXPAND(TW_CUSTOM_BATTERY_PATH)) + "/" + name;
#else
  return std::string("/sys/class/power_supply/battery/") + name;
#endif
}

bool read_legacy_battery(int* percentage, bool* charging) {
  bool valid = false;
  std::ifstream capacity_file(battery_file("capacity"));
  int value = -1;
  if (capacity_file >> value) {
    if (value >= 0 && value <= 100) {
      *percentage = value;
      valid = true;
    }
  }

  std::ifstream status_file(battery_file("status"));
  std::string status;
  if (status_file >> status) *charging = !status.empty() && status[0] == 'C';

  return valid;
}
#endif

}  // namespace

status_backend::status_backend(settings_store* settings) : settings_(settings) {}

status_backend::~status_backend() {
  stop();
}

void status_backend::start() {
  if (worker_.joinable()) return;

  {
    std::lock_guard<std::mutex> lock(wait_mutex_);
    stop_requested_ = false;
  }
  worker_ = std::thread(&status_backend::run, this);
}

void status_backend::stop() {
  {
    std::lock_guard<std::mutex> lock(wait_mutex_);
    stop_requested_ = true;
  }
  wait_condition_.notify_all();
  if (worker_.joinable()) worker_.join();
}

status_snapshot status_backend::snapshot() const {
  status_snapshot snapshot;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot = state_;
  }

  // Reflect a newly applied clock format immediately.
  const bool military_time = settings_ != nullptr && settings_->get_int("tw_military_time", 0) != 0;
  snapshot.time_text = format_time(military_time);
  return snapshot;
}

void status_backend::run() {
  for (;;) {
    update_once();

    std::unique_lock<std::mutex> lock(wait_mutex_);
    if (wait_condition_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_requested_; }))
      return;
  }
}

void status_backend::update_once() {
  const bool military_time = settings_ != nullptr && settings_->get_int("tw_military_time", 0) != 0;
  status_snapshot next;
  next.time_text = format_time(military_time);

#ifdef TW_USE_LEGACY_BATTERY_SERVICES
  int percentage = 0;
  bool charging = false;
  next.battery_valid = read_legacy_battery(&percentage, &charging);
  if (next.battery_valid) {
    next.battery_percentage = percentage;
    next.charging = charging;
  }
#else
  const BatteryInfo battery = GetBatteryInfo();
  next.battery_valid = battery.capacity >= 0 && battery.capacity <= 100;
  next.battery_percentage = std::clamp(battery.capacity, 0, 100);
  next.charging = battery.charging;
#endif

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!next.battery_valid && state_.battery_valid) {
    next.battery_percentage = state_.battery_percentage;
    next.charging = state_.charging;
  }
  state_ = std::move(next);
}

}  // namespace gui2_backend
