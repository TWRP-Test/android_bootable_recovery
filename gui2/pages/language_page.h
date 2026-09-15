#ifndef GUI2_PAGES_LANGUAGE_PAGE_H
#define GUI2_PAGES_LANGUAGE_PAGE_H

#include <cstddef>

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct language_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* option_cards[3] = {};
  lv_obj_t* check_labels[3] = {};
};

struct language_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const gui2_i18n::language_id* languages = nullptr;
  size_t language_count = 0;
  gui2_i18n::language_id pending_language = gui2_i18n::language_id::ENGLISH;
  lv_event_cb_t option_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

language_page_view build_language_page(const language_page_options& options);

}  // namespace gui2_pages

#endif
