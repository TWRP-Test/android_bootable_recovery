#ifndef GUI2_BACKEND_TERMINAL_BACKEND_H
#define GUI2_BACKEND_TERMINAL_BACKEND_H

#include <cstddef>
#include <string>

namespace gui2_backend {

// The shell the legacy terminal runs, driven through the engine that already
// owns it. The engine keeps the text buffer and does the escape handling, so
// this deals in lines of finished text rather than a byte stream.
class terminal_backend {
 public:
  virtual ~terminal_backend() = default;

  // Starts the shell if it is not already running.
  virtual bool start() = 0;
  virtual bool running() = 0;
  virtual void stop() = 0;

  // How wide and tall the buffer should wrap, in characters.
  virtual void set_size(int columns, int rows) = 0;

  // Reads whatever the shell produced. The page calls this from its own loop.
  virtual void pump() = 0;

  // Changes whenever the buffer changed, so the page can skip a redraw.
  virtual int update_counter() = 0;
  virtual size_t line_count() = 0;
  virtual std::string line(size_t index) = 0;

  virtual void send_line(const std::string& text) = 0;
  // Control keys: 0x03 interrupts, 0x04 ends input.
  virtual void send_byte(char byte) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TERMINAL_BACKEND_H
