#include "twrp_console_backend.h"

#include <string>

#include "gui/objects.hpp"

namespace gui2_backend {
namespace {

console_severity severity_from_color(const std::string& color) {
  if (color == "error") return console_severity::ERROR;
  if (color == "warning") return console_severity::WARNING;
  if (color == "highlight") return console_severity::HIGHLIGHT;
  return console_severity::NORMAL;
}

}  // namespace

size_t twrp_console_backend::fetch(size_t from, std::vector<console_line>* lines) {
  std::vector<std::string> text;
  std::vector<std::string> colors;
  const size_t total = GUIConsole::Get_Lines(from, lines == nullptr ? nullptr : &text,
                                             lines == nullptr ? nullptr : &colors);
  if (lines == nullptr) return total;

  lines->reserve(lines->size() + text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    console_line line;
    line.text = std::move(text[i]);
    line.severity = severity_from_color(i < colors.size() ? colors[i] : std::string("normal"));
    lines->push_back(std::move(line));
  }
  return total;
}

}  // namespace gui2_backend
