#include "twrp_settings_store.h"

#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"
#include "twrp_operation.h"

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

bool twrp_settings_store::set(const std::string& key, const std::string& value) {
  return DataManager::SetValue(key, value) == 0;
}

bool twrp_settings_store::flush() {
  return DataManager::Flush() == 0;
}

// GUIAction::setguitimezone
void twrp_settings_store::update_timezone() {
  std::string SelectedZone;
  DataManager::GetValue(TW_TIME_ZONE_GUISEL, SelectedZone);  // read the selected time zone into SelectedZone
  std::string Zone = SelectedZone.substr(0, SelectedZone.find(';'));  // parse to get time zone
  std::string DSTZone = SelectedZone.substr(SelectedZone.find(';') + 1, std::string::npos);  // parse to get DST component

  int dst;
  DataManager::GetValue(TW_TIME_ZONE_GUIDST, dst);  // check wether user chose to use DST

  std::string offset;
  DataManager::GetValue(TW_TIME_ZONE_GUIOFFSET, offset);  // pull in offset

  std::string NewTimeZone = Zone;
  if (offset != "0") NewTimeZone += ":" + offset;

  if (dst != 0) NewTimeZone += DSTZone;

  DataManager::SetValue(TW_TIME_ZONE_VAR, NewTimeZone);
  DataManager::update_tz_environment_variables();
}

// GUIAction::restoredefaultsettings
bool twrp_settings_store::restore_defaults() {
  operation_start("Restore Defaults");
  DataManager::ResetDefaults();
  PartitionManager.Update_System_Details();
  PartitionManager.Mount_Current_Storage(true);
  operation_end(0);
  return true;
}

}  // namespace gui2_backend
