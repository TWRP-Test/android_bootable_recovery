#include "status_backend.h"

#include <cstdlib>
#include <string>

#include "settings_store.h"

namespace gui2_backend {

status_snapshot status_backend::snapshot() const {
  status_snapshot snapshot;
  if (settings_ == nullptr) return snapshot;
  snapshot.time_text = settings_->get_string("tw_time", "");

  // "<capacity>%<'+' when charging, else ' '>"; -1 until the first reading.
  const std::string battery = settings_->get_string("tw_battery", "");
  const size_t percent = battery.find('%');
  if (percent == std::string::npos || settings_->get_int("tw_no_battery_percent", 0) != 0)
    return snapshot;
  snapshot.battery_percentage = std::atoi(battery.substr(0, percent).c_str());
  snapshot.charging = percent + 1 < battery.size() && battery[percent + 1] == '+';
  // ui.xml shows it while 0 < tw_battery < 101.
  snapshot.battery_valid = snapshot.battery_percentage > 0 && snapshot.battery_percentage < 101;
  return snapshot;
}

}  // namespace gui2_backend
