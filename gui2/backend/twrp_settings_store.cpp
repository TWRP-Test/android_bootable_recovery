#include "twrp_settings_store.h"

#include "data.hpp"

namespace gui2_backend {

std::string twrp_settings_store::get_string(const std::string& key,
                                            const std::string& fallback) const {
  std::string value;
  return DataManager::GetValue(key, value) == 0 ? value : fallback;
}

int twrp_settings_store::get_int(const std::string& key, int fallback) const {
  int value;
  return DataManager::GetValue(key, value) == 0 ? value : fallback;
}

bool twrp_settings_store::set_persistent(const std::string& key, const std::string& value) {
  return DataManager::SetValue(key, value, 1) == 0;
}

bool twrp_settings_store::flush() {
  return DataManager::Flush() == 0;
}

void twrp_settings_store::update_timezone() {
  DataManager::update_tz_environment_variables();
}

}  // namespace gui2_backend
