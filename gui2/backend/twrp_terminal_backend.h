#ifndef GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H
#define GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H

#include <string>

#include "terminal_backend.h"

namespace gui2_backend {

class twrp_terminal_backend final : public terminal_backend {
 public:
  ~twrp_terminal_backend() override;

  bool start() override;
  bool running() override;
  void stop() override;
  std::string working_directory() override;
  bool send_line(const std::string& line) override;
  bool send_byte(char byte) override;
  std::string take_output() override;

 private:
  // Drops the escape sequences a shell emits for colour and cursor moves. The
  // page shows plain lines, so anything that is not text would arrive as
  // rubbish.
  void append_filtered(const char* data, size_t length);

  int master_ = -1;
  int pid_ = 0;
  std::string pending_;
  // Half-finished escape sequence carried to the next read.
  std::string escape_;
  bool in_escape_ = false;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H
