#ifndef GUI2_BACKEND_CONSOLE_BACKEND_H
#define GUI2_BACKEND_CONSOLE_BACKEND_H

#include <cstddef>
#include <string>
#include <vector>

namespace gui2_backend {

enum class console_severity {
  NORMAL,
  HIGHLIGHT,
  WARNING,
  ERROR,
};

struct console_line {
  std::string text;
  console_severity severity = console_severity::NORMAL;
};

// Read-only view of the recovery console shared with the legacy GUI.
class console_backend {
 public:
  virtual ~console_backend() = default;

  // Appends the lines recorded after `from` and returns the new total count.
  virtual size_t fetch(size_t from, std::vector<console_line>* lines) = 0;

  // The legacy language files' strings, which console messages are written
  // in; empty when a key has none.
  virtual void load_strings(const std::string& language) = 0;
  virtual std::string translate(const std::string& key) = 0;
  // What setlanguage does besides: partition names and console lines follow.
  virtual void retranslate(const std::string& language) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_CONSOLE_BACKEND_H
