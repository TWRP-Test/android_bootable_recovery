#ifndef GUI2_BACKEND_LOG_EXPORT_BACKEND_H
#define GUI2_BACKEND_LOG_EXPORT_BACKEND_H

#include <string>

namespace gui2_backend {

class log_export_backend {
 public:
  virtual ~log_export_backend() = default;

  // tw_logcat_exists: the logcat checkbox is only offered with it.
  virtual bool has_logcat() const = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_LOG_EXPORT_BACKEND_H
