#ifndef GUI2_BACKEND_STARTUP_BACKEND_H
#define GUI2_BACKEND_STARTUP_BACKEND_H

#include <string>

namespace gui2_backend {

// In the order the recovery runs them.
enum class startup_step {
  PARTITIONS,
  STORAGE,
  SETTINGS,
  SYSTEM,
  SCRIPTS,
  SERVICES,
  FINISHING,
  DONE,
};

enum class startup_pause {
  NONE,
  // OpenRecoveryScript is running on the startup thread.
  SCRIPT,
  // Waiting for answer_system_read_only().
  SYSTEM_READ_ONLY,
};

struct startup_status {
  startup_step step = startup_step::PARTITIONS;
  startup_pause pause = startup_pause::NONE;
  bool failed = false;
};

class startup_backend {
 public:
  virtual ~startup_backend() = default;

  virtual void start() = 0;
  virtual startup_status status() = 0;
  virtual void finish() = 0;

  virtual void answer_system_read_only(bool keep_read_only, bool never_show_again) = 0;
  virtual bool can_hide_system_read_only() = 0;

  virtual std::string device_label() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_STARTUP_BACKEND_H
