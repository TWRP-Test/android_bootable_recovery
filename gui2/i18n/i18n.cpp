#include "i18n.h"

namespace gui2_i18n {

extern const language_pack kEnglish;
extern const language_pack kSimplifiedChinese;
extern const language_pack kTraditionalChinese;

const language_pack& get_language_pack(language_id language) {
  switch (language) {
    case language_id::ENGLISH:
      return kEnglish;
    case language_id::ZH_TW:
      return kTraditionalChinese;
    case language_id::ZH_CN:
    default:
      return kSimplifiedChinese;
  }
}

}  // namespace gui2_i18n
