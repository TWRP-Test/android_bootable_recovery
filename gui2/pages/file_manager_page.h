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
  // Set: a tap arrives as LV_EVENT_SHORT_CLICKED, which LVGL skips once a
  // long press fired, and a hold as LV_EVENT_LONG_PRESSED.
  lv_event_cb_t entry_long_press_callback = nullptr;

  // File sizes on the right of each row, and what an empty list says.
  bool show_sizes = false;
  const char* empty_text = nullptr;

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

// A quick fade for a list that changed in place, e.g. after a new sort.
void fade_file_list(const file_manager_page_view& view);

// The row under the trail: a search field and, when sort_label is set, the
// sort button. Both stay put while the list scrolls.
struct file_toolbar_options {
  lv_obj_t* parent = nullptr;
  int y = 0;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const char* query = nullptr;
  lv_event_cb_t input_callback = nullptr;  // LV_EVENT_VALUE_CHANGED of the field
  lv_event_cb_t clear_callback = nullptr;
  const char* sort_label = nullptr;
  lv_event_cb_t sort_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct file_toolbar_view {
  lv_obj_t* root = nullptr;
  lv_obj_t* field = nullptr;
  lv_obj_t* input = nullptr;
  lv_obj_t* clear = nullptr;
  lv_obj_t* sort_button = nullptr;
  lv_obj_t* sort_label = nullptr;
};

file_toolbar_view build_file_toolbar(const file_toolbar_options& options);
int file_toolbar_height(const gui2_core::ui_metrics& metrics);
// The field's highlight while the keyboard is up.
void set_file_toolbar_active(const file_toolbar_view& view, bool active);
void show_file_toolbar_clear(const file_toolbar_view& view, bool shown);

// A small menu under the sort button. The callback's user data points into
// indices; tapping outside calls dismiss_callback.
struct sort_menu_options {
  lv_obj_t* anchor = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const char* const* labels = nullptr;
  size_t count = 0;
  size_t selected = 0;
  // A thin line goes after every group_size rows.
  size_t group_size = 2;
  const int* indices = nullptr;
  lv_event_cb_t choice_callback = nullptr;
  lv_event_cb_t dismiss_callback = nullptr;
};

// Returns the backdrop, which owns the menu.
lv_obj_t* open_sort_menu(const sort_menu_options& options);
void close_sort_menu(lv_obj_t* backdrop, bool animate);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_FILE_MANAGER_PAGE_H
