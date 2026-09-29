#include "twrp_log_export_backend.h"

namespace gui2_backend {

bool twrp_log_export_backend::has_logcat() const {
  return settings_ == nullptr ? false : settings_->get_int("tw_logcat_exists", 0) != 0;
}

}  // namespace gui2_backend
