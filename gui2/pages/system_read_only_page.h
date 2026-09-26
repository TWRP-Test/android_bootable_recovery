#ifndef GUI2_PAGES_SYSTEM_READ_ONLY_PAGE_H
#define GUI2_PAGES_SYSTEM_READ_ONLY_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// Legacy system_readonly page. The caller adds the "allow modifications" swipe.
struct system_read_only_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  bool show_never_show = false;
  bool never_show = false;
  lv_event_cb_t never_show_callback = nullptr;
  lv_event_cb_t keep_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

void build_system_read_only_page(const system_read_only_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_SYSTEM_READ_ONLY_PAGE_H
