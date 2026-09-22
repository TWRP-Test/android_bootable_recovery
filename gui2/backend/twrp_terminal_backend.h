#ifndef GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H
#define GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H

#include <cstddef>
#include <string>

#include "terminal_backend.h"

namespace gui2_backend {

// Everything here forwards to the engine in gui/terminal.cpp. That engine says
// itself that any number of front ends may share one instance, so gui2 uses the
// one the legacy terminal page already has rather than opening a second shell.
class twrp_terminal_backend final : public terminal_backend {
 public:
  bool start() override;
  bool running() override;
  void stop() override;
  void set_size(int columns, int rows) override;
  void pump() override;
  int update_counter() override;
  size_t line_count() override;
  std::string line(size_t index) override;
  void send_line(const std::string& text) override;
  void send_byte(char byte) override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H
