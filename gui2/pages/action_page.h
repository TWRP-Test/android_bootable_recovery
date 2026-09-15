#ifndef GUI2_PAGES_ACTION_PAGE_H
#define GUI2_PAGES_ACTION_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/action_definitions.h"

namespace gui2_pages {

struct action_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const action_definition* definition = nullptr;
};

// Builds the common action description and returns the body that owns it.
// Settings-specific rows are appended by the caller because their callbacks
// depend on recovery services and page routing.
lv_obj_t* build_action_page(const action_page_options& options);

}  // namespace gui2_pages

#endif
