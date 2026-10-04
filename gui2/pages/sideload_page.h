#ifndef GUI2_PAGES_SIDELOAD_PAGE_H
#define GUI2_PAGES_SIDELOAD_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// Legacy sideload page. The caller adds the "start sideload" swipe.
struct sideload_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  bool wipe_dalvik = false;
  bool wipe_cache = false;
  // user_data is 0 for the Dalvik row, 1 for the cache row.
  lv_event_cb_t option_callback = nullptr;
};

void build_sideload_page(const sideload_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_SIDELOAD_PAGE_H
