#ifndef GUI2_PAGES_PAGE_STATE_H
#define GUI2_PAGES_PAGE_STATE_H

#include "backend/hardware_settings.h"
#include "components/slider.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct hardware_slider_binding {
  lv_obj_t* value_label = nullptr;
  gui2_backend::haptic_channel channel = gui2_backend::haptic_channel::BUTTON;
  bool brightness = false;
  bool recording_fps = false;
  gui2_components::slider visual;
};

struct page_state {
  gui2_i18n::language_id current_language = gui2_i18n::language_id::ZH_CN;
  gui2_i18n::language_id pending_language = gui2_i18n::language_id::ZH_CN;
  lv_obj_t* language_option_cards[3] = {};
  lv_obj_t* language_check_labels[3] = {};

  int pending_timezone_index = 0;
  int pending_offset_index = 0;
  bool pending_military_time = false;
  bool pending_dst = false;
  lv_obj_t* timezone_cards[24] = {};
  lv_obj_t* offset_cards[4] = {};
  lv_obj_t* format_cards[2] = {};
  lv_obj_t* dst_card = nullptr;
  lv_obj_t* current_timezone_label = nullptr;

  lv_obj_t* hardware_error_label = nullptr;
  bool hardware_settings_dirty = false;
  hardware_slider_binding brightness_binding;
  hardware_slider_binding quick_brightness_binding;
  hardware_slider_binding haptic_bindings[3];
  hardware_slider_binding recording_binding;
  bool quick_brightness_dirty = false;
};

}  // namespace gui2_pages

#endif
