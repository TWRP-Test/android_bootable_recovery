#ifndef GUI2_PAGES_GENERAL_SETTINGS_PAGE_H
#define GUI2_PAGES_GENERAL_SETTINGS_PAGE_H

#include <cstddef>

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// One switch on the page. group is the heading printed above it, or nullptr to
// stay under the previous one; key is the recovery variable it writes.
struct general_setting {
  const char* group = nullptr;
  const char* label = nullptr;
  const char* key = nullptr;
  bool value = false;
};

struct general_settings_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const general_setting* items = nullptr;
  size_t item_count = 0;
  // Stable per row, because the switch hands its index back through the event.
  const int* item_indices = nullptr;
  lv_event_cb_t toggle_callback = nullptr;
};

void build_general_settings_page(const general_settings_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_GENERAL_SETTINGS_PAGE_H
