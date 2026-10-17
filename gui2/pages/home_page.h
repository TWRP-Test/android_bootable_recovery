#ifndef GUI2_PAGES_HOME_PAGE_H
#define GUI2_PAGES_HOME_PAGE_H

#include <cstddef>

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/action_definitions.h"

namespace gui2_pages {

struct home_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const action_definition* actions = nullptr;
  size_t action_count = 0;
  lv_event_cb_t action_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const char* notice_text = nullptr;
  lv_event_cb_t notice_event_callback = nullptr;
};

void build_home_page(const home_page_options& options);

}  // namespace gui2_pages

#endif
