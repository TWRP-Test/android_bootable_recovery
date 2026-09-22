#ifndef GUI2_BACKEND_TERMINAL_BACKEND_H
#define GUI2_BACKEND_TERMINAL_BACKEND_H

#include <string>

namespace gui2_backend {

// A shell on a pseudoterminal, the way the legacy terminal page runs one. The
// page types whole lines rather than single keys, so this interface deals in
// lines and in the raw bytes the control keys send.
class terminal_backend {
 public:
  virtual ~terminal_backend() = default;

  // Starts the shell if it is not already running.
  virtual bool start() = 0;
  virtual bool running() = 0;
  virtual void stop() = 0;

  // The directory the shell is in, for the prompt.
  virtual std::string working_directory() = 0;

  virtual bool send_line(const std::string& line) = 0;
  // Control keys: 0x03 interrupts, 0x04 ends input.
  virtual bool send_byte(char byte) = 0;

  // Whatever the shell has written since the last call, with the escape
  // sequences already taken out. Empty when there is nothing new.
  virtual std::string take_output() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TERMINAL_BACKEND_H
