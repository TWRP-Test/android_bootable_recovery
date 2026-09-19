#ifndef GUI2_PAGES_WIPE_PAGE_H
#define GUI2_PAGES_WIPE_PAGE_H

#include <cstddef>

#include "backend/wipe_backend.h"
#include "components/swipe_slider.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

int wipe_track_height();
int wipe_hint_height(const gui2_core::ui_metrics& metrics, const char* text);
int format_data_keyboard_height(const gui2_core::ui_metrics& metrics);

struct wipe_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* page_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  lv_event_cb_t option_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const void* advanced_target = nullptr;
  const void* format_data_target = nullptr;
  bool has_data_media = false;
  gui2_components::swipe_slider* confirm = nullptr;
  void (*confirm_callback)(void*) = nullptr;
  void* confirm_user_data = nullptr;
};

void build_wipe_page(const wipe_page_options& options);

struct advanced_wipe_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const gui2_backend::wipe_target* targets = nullptr;
  size_t target_count = 0;
  const bool* selected = nullptr;
  lv_event_cb_t option_event_callback = nullptr;
  const int* target_indices = nullptr;
};

void build_advanced_wipe_page(const advanced_wipe_page_options& options);

struct format_data_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* page_layer = nullptr;
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  lv_event_cb_t input_event_callback = nullptr;
  lv_event_cb_t keyboard_event_callback = nullptr;
  gui2_components::swipe_slider* confirm = nullptr;
  void (*confirm_callback)(void*) = nullptr;
  void* confirm_user_data = nullptr;
};

struct format_data_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* input = nullptr;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* slider_track = nullptr;
};

format_data_page_view build_format_data_page(const format_data_page_options& options);

}  // namespace gui2_pages

#endif
