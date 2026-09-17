#ifndef GUI2_PAGES_EXPORT_LOG_PAGE_H
#define GUI2_PAGES_EXPORT_LOG_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct export_log_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  lv_event_cb_t option_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const void* kernel_log_target = nullptr;
  const void* logcat_target = nullptr;
  bool include_kernel_log = false;
  bool include_logcat = false;
  bool has_logcat = false;
};

struct export_log_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* kernel_log_card = nullptr;
  lv_obj_t* logcat_card = nullptr;
  lv_obj_t* result_label = nullptr;
};

export_log_page_view build_export_log_page(const export_log_page_options& options);

}  // namespace gui2_pages

#endif
