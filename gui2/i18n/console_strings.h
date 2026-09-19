#ifndef GUI2_I18N_CONSOLE_STRINGS_H
#define GUI2_I18N_CONSOLE_STRINGS_H

#include <string>

#include "i18n/i18n.h"

namespace gui2_i18n {

// Console output is raised by the legacy code as a resource key plus an English
// default. Returns the translation for that key, or nullptr to leave the
// English default in place.
const char* console_string_for(language_id language, const std::string& key);

}  // namespace gui2_i18n

#endif  // GUI2_I18N_CONSOLE_STRINGS_H
