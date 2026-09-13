#ifndef GUI2_BACKEND_TWRP_SETTINGS_STORE_H
#define GUI2_BACKEND_TWRP_SETTINGS_STORE_H

#include "settings_store.h"

namespace gui2_backend {

class twrp_settings_store final : public settings_store {
 public:
  std::string get_string(const std::string& key, const std::string& fallback) const override;
  int get_int(const std::string& key, int fallback) const override;
  bool set_persistent(const std::string& key, const std::string& value) override;
  bool flush() override;
  void update_timezone() override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_SETTINGS_STORE_H
