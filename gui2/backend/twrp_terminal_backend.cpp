#include "twrp_terminal_backend.h"

#include "gui/terminal.hpp"

namespace gui2_backend {

bool twrp_terminal_backend::start() {
  twrp_terminal::start();
  return twrp_terminal::running();
}

bool twrp_terminal_backend::running() {
  return twrp_terminal::running();
}

void twrp_terminal_backend::stop() {
  twrp_terminal::stop();
}

void twrp_terminal_backend::set_size(int columns, int rows) {
  twrp_terminal::set_size(columns, rows);
}

void twrp_terminal_backend::pump() {
  twrp_terminal::pump();
}

int twrp_terminal_backend::update_counter() {
  return twrp_terminal::update_counter();
}

size_t twrp_terminal_backend::line_count() {
  return twrp_terminal::line_count();
}

std::string twrp_terminal_backend::line(size_t index) {
  return twrp_terminal::line(index);
}

// The engine takes input a character at a time, the way a keyboard feeds it.
void twrp_terminal_backend::send_line(const std::string& text) {
  for (const char c : text) twrp_terminal::input_char(static_cast<unsigned char>(c));
  twrp_terminal::input_char('\n');
}

void twrp_terminal_backend::send_byte(char byte) {
  twrp_terminal::input_char(static_cast<unsigned char>(byte));
}

}  // namespace gui2_backend
