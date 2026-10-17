#ifndef GUI2_BACKEND_SETTINGS_STORE_H
#define GUI2_BACKEND_SETTINGS_STORE_H

#include <string>

namespace gui2_backend {

// Persistence adapter used by GUI2 pages.
class settings_store {
 public:
  virtual ~settings_store() = default;
  virtual std::string get_string(const std::string& key, const std::string& fallback) const = 0;
  virtual int get_int(const std::string& key, int fallback) const = 0;
  virtual bool set_persistent(const std::string& key, const std::string& value) = 0;
  // The legacy "set" action: a persistent variable stays one, anything else
  // lives only in memory.
  virtual bool set(const std::string& key, const std::string& value) = 0;
  virtual bool flush() = 0;
  // setguitimezone: tw_time_zone from the three tw_time_zone_gui* choices.
  virtual void update_timezone() = 0;
  // Puts every recovery variable back to its built-in value and
  // remounts storage, the way the legacy Restore Defaults button does.
  virtual bool restore_defaults() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_SETTINGS_STORE_H
