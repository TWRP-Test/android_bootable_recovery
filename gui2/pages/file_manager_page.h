#ifndef GUI2_PAGES_FILE_MANAGER_PAGE_H
#define GUI2_PAGES_FILE_MANAGER_PAGE_H

#include <cstddef>

#include "backend/file_manager_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct file_manager_page_options {
  lv_obj_t* content = nullptr;
  // The trail belongs outside the scrolling area, so it stays put while the
  // list moves under it. Null keeps it inside the body.
  lv_obj_t* crumb_parent = nullptr;
  int crumb_y = 0;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;

  // One pill per path component, the last one marked as where we are.
  const char* const* crumbs = nullptr;
  size_t crumb_count = 0;
  const int* crumb_indices = nullptr;
  lv_event_cb_t crumb_callback = nullptr;

  // The way up lives at the top of the list rather than on the back key, so
  // the trail and the list agree on where you are.
  bool show_parent_row = false;
  const char* parent_label = nullptr;
  lv_event_cb_t parent_callback = nullptr;

  const gui2_backend::file_entry* entries = nullptr;
  size_t entry_count = 0;
  const int* entry_indices = nullptr;
  lv_event_cb_t entry_callback = nullptr;

  lv_event_cb_t press_guard_callback = nullptr;
};

struct file_manager_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* crumbs = nullptr;
  lv_obj_t* list = nullptr;
};

file_manager_page_view build_file_manager_page(const file_manager_page_options& options);

// Slides the list in from one side. Changing folder rebuilds only the list, so
// the heading, the floating button and the navigation stay put instead of
// flickering through a page transition.
void animate_file_list(const file_manager_page_view& view, const gui2_core::ui_metrics& metrics,
                       bool deeper);

// How much room the fixed trail needs above the list.
int crumb_bar_height(const gui2_core::ui_metrics& metrics);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_FILE_MANAGER_PAGE_H
