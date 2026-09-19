#ifndef GUI2_BACKEND_DECRYPT_BACKEND_H
#define GUI2_BACKEND_DECRYPT_BACKEND_H

#include <string>

namespace gui2_backend {

// How the device asks for the user 0 credential. Mirrors the values TWRP stores
// in tw_crypto_pwtype.
enum class lock_kind {
  DEFAULT,
  PASSWORD,
  PATTERN,
  PIN,
};

enum class decrypt_state {
  IDLE,
  RUNNING,
  DONE,
  FAILED,
};

// Decryption of the primary user. Recovery only ever needs user 0 mounted, so
// the other users the device may carry are deliberately out of scope.
class decrypt_backend {
 public:
  virtual ~decrypt_backend() = default;

  virtual bool is_encrypted() = 0;
  virtual lock_kind kind() = 0;

  // Runs the attempt on a worker so the UI keeps drawing. Returns false when a
  // previous attempt is still in flight.
  virtual bool start(const std::string& password) = 0;

  // Cancelling the unlock leaves every size stale, which is why the legacy UI
  // recomputes them on its way out.
  virtual bool start_refresh() = 0;
  virtual decrypt_state state() = 0;

  // Clears a DONE/FAILED result so the page can be entered again.
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_DECRYPT_BACKEND_H
