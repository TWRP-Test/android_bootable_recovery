#ifndef GUI2_PAGES_ADVANCED_PAGE_H
#define GUI2_PAGES_ADVANCED_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct advanced_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  lv_event_cb_t option_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const void* export_log_target = nullptr;
};

void build_advanced_page(const advanced_page_options& options);

}  // namespace gui2_pages

#endif
