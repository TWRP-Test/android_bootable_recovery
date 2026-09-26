#ifndef GUI2_BACKEND_SIDELOAD_BACKEND_H
#define GUI2_BACKEND_SIDELOAD_BACKEND_H

namespace gui2_backend {

enum class sideload_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
  CANCELLED,
};

struct sideload_status {
  sideload_state state = sideload_state::IDLE;
  // 0-100 once the zip reports progress; -1 while waiting for the computer
  // or when the zip never reports any.
  int progress = -1;
};

class sideload_backend {
 public:
  virtual ~sideload_backend() = default;

  virtual bool start(bool wipe_dalvik, bool wipe_cache) = 0;
  virtual void cancel() = 0;
  virtual sideload_status status() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_SIDELOAD_BACKEND_H
