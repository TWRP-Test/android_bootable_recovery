#ifndef GUI2_PAGES_FILE_ACTIONS_PAGE_H
#define GUI2_PAGES_FILE_ACTIONS_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// The legacy filemanageroptions page. chmod with a custom value and the two
// rename variants need a text field, which the manager page owns, so they are
// not here yet.
struct file_actions_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  bool is_folder = false;
  lv_event_cb_t callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const void* terminal_target = nullptr;
  const void* copy_target = nullptr;
  const void* move_target = nullptr;
  const void* chmod755_target = nullptr;
  const void* chmod_target = nullptr;
  const void* rename_target = nullptr;
  const void* delete_target = nullptr;
};

void build_file_actions_page(const file_actions_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_FILE_ACTIONS_PAGE_H
