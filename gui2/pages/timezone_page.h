#ifndef GUI2_PAGES_TIMEZONE_PAGE_H
#define GUI2_PAGES_TIMEZONE_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct timezone_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* timezone_cards[24] = {};
  lv_obj_t* offset_cards[4] = {};
  lv_obj_t* format_cards[2] = {};
  lv_obj_t* dst_card = nullptr;
  lv_obj_t* current_timezone_label = nullptr;
};

struct timezone_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const int* timezone_indices = nullptr;
  const int* offset_indices = nullptr;
  const int* format_indices = nullptr;
  const char* current_timezone_text = nullptr;
  lv_event_cb_t timezone_event_callback = nullptr;
  lv_event_cb_t offset_event_callback = nullptr;
  lv_event_cb_t format_event_callback = nullptr;
  lv_event_cb_t dst_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

timezone_page_view build_timezone_page(const timezone_page_options& options);

}  // namespace gui2_pages

#endif
