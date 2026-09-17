#ifndef GUI2_PAGES_WIPE_PROGRESS_PAGE_H
#define GUI2_PAGES_WIPE_PROGRESS_PAGE_H

#include "backend/wipe_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/console_page.h"

namespace gui2_pages {

struct wipe_progress_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const lv_font_t* console_font = nullptr;
};

struct wipe_progress_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* state_label = nullptr;
  lv_obj_t* bar = nullptr;
  lv_obj_t* bar_fill = nullptr;
  console_page_view console;
};

wipe_progress_page_view build_wipe_progress_page(const wipe_progress_page_options& options);

void update_wipe_progress(const wipe_progress_page_view& view,
                          const gui2_i18n::language_pack& strings,
                          const gui2_backend::wipe_status& status);

}  // namespace gui2_pages

#endif
