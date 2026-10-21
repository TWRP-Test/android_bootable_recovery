#include "i18n.h"

namespace gui2_i18n {

extern const language_pack kEnglish;
extern const language_pack kSimplifiedChinese;
extern const language_pack kTraditionalChinese;
extern const language_pack kRussian;

const language_pack& get_language_pack(language_id language) {
  switch (language) {
    case language_id::ZH_CN:
      return kSimplifiedChinese;
    case language_id::ZH_TW:
      return kTraditionalChinese;
    case language_id::RUSSIAN:
      return kRussian;
    case language_id::ENGLISH:
    default:
      return kEnglish;
  }
}

const char* language_code(language_id language) {
  switch (language) {
    case language_id::ZH_CN:
      return "zh_CN";
    case language_id::ZH_TW:
      return "zh_TW";
    case language_id::RUSSIAN:
      return "ru";
    case language_id::ENGLISH:
    default:
      return "en";
  }
}

language_id language_from_code(std::string_view code) {
  for (const auto language : kLanguageValues) {
    if (code == language_code(language)) return language;
  }
  return language_id::ENGLISH;
}

}  // namespace gui2_i18n
