#ifndef GUI2_BACKEND_SETTINGS_STORE_H
#define GUI2_BACKEND_SETTINGS_STORE_H

#include <string>

namespace gui2_backend {

// The UI uses this small interface instead of knowing anything about
// DataManager or the on-disk TWRP settings format.  Recovery supplies the
// DataManager implementation.
class settings_store {
 public:
  virtual ~settings_store() = default;
  virtual std::string get_string(const std::string& key, const std::string& fallback) const = 0;
  virtual int get_int(const std::string& key, int fallback) const = 0;
  virtual bool set_persistent(const std::string& key, const std::string& value) = 0;
  virtual bool flush() = 0;
  virtual void update_timezone() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_SETTINGS_STORE_H
