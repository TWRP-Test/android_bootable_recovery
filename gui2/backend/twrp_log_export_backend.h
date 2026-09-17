#ifndef GUI2_BACKEND_TWRP_LOG_EXPORT_BACKEND_H
#define GUI2_BACKEND_TWRP_LOG_EXPORT_BACKEND_H

#include "log_export_backend.h"
#include "settings_store.h"

namespace gui2_backend {

class twrp_log_export_backend final : public log_export_backend {
 public:
  explicit twrp_log_export_backend(settings_store* settings) : settings_(settings) {}

  bool has_logcat() const override;
  log_export_result export_logs(bool include_kernel_log, bool include_logcat) override;

 private:
  settings_store* settings_;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_LOG_EXPORT_BACKEND_H
