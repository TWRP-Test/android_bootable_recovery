#include "twrp_log_export_backend.h"

#include <unistd.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "twrpinstall/include/set_metadata.h"

namespace gui2_backend {

bool twrp_log_export_backend::has_logcat() const {
  return settings_ == nullptr ? false : settings_->get_int("tw_logcat_exists", 0) != 0;
}

log_export_result twrp_log_export_backend::export_logs(bool include_kernel_log,
                                                       bool include_logcat) {
  log_export_result result;
  if (PartitionManager.Mount_Current_Storage(false) == 0) return result;

  const std::string storage = DataManager::GetCurrentStoragePath();
  if (storage.empty()) return result;

  const std::string destination = storage + "/recovery.log";
  if (TWFunc::copy_file("/tmp/recovery.log", destination, 0755) != 0) return result;
  tw_set_default_metadata(destination.c_str());

  if (include_kernel_log) TWFunc::copy_kernel_log(storage);
  if (include_logcat) TWFunc::copy_logcat(storage);
  sync();

  result.success = true;
  result.path = destination;
  return result;
}

}  // namespace gui2_backend
