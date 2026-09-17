#ifndef GUI2_BACKEND_LOG_EXPORT_BACKEND_H
#define GUI2_BACKEND_LOG_EXPORT_BACKEND_H

#include <string>

namespace gui2_backend {

struct log_export_result {
  bool success = false;
  std::string path;
};

class log_export_backend {
 public:
  virtual ~log_export_backend() = default;

  virtual bool has_logcat() const = 0;
  virtual log_export_result export_logs(bool include_kernel_log, bool include_logcat) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_LOG_EXPORT_BACKEND_H
