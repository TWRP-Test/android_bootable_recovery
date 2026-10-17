#ifndef GUI2_BACKEND_STATUS_BACKEND_H
#define GUI2_BACKEND_STATUS_BACKEND_H

#include <string>

namespace gui2_backend {

class settings_store;

struct status_snapshot {
  std::string time_text;
  int battery_percentage = 0;
  bool charging = false;
  bool battery_valid = false;
};

// The status bar values the legacy theme shows: tw_time, and tw_battery as the
// battery monitor in twrp.cpp writes it.
class status_backend final {
 public:
  explicit status_backend(settings_store* settings) : settings_(settings) {}

  status_snapshot snapshot() const;

 private:
  settings_store* settings_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_STATUS_BACKEND_H
