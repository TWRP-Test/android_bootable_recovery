#ifndef GUI2_PAGES_PAGE_STATE_H
#define GUI2_PAGES_PAGE_STATE_H

#include <cstddef>
#include <cstdint>

#include "backend/hardware_settings.h"
#include "backend/reboot_backend.h"
#include "components/slider.h"
#include "components/swipe_slider.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/console_page.h"
#include "pages/wipe_progress_page.h"
#include "pages/page_router.h"
#include "pages/reboot_page.h"

namespace gui2_pages {

struct hardware_slider_binding {
  lv_obj_t* value_label = nullptr;
  gui2_backend::haptic_channel channel = gui2_backend::haptic_channel::BUTTON;
  bool brightness = false;
  bool recording_fps = false;
  bool screen_timeout = false;
  bool console_font = false;
  gui2_components::slider visual;
};

struct reboot_page_state {
  bool target_selected = false;
  bool has_error = false;
  gui2_backend::reboot_target selected_target = gui2_backend::reboot_target::SYSTEM;
  gui2_pages::page_request return_request{ gui2_pages::page_id::HOME, nullptr,
                                           gui2_core::page_transition::NONE };
  reboot_option options[7] = {};
  gui2_backend::boot_slot slots[2] = { gui2_backend::boot_slot::A, gui2_backend::boot_slot::B };
  gui2_components::swipe_slider confirmation_slider;
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
  hardware_slider_binding screen_timeout_binding;
  hardware_slider_binding console_font_binding;
  hardware_slider_binding quick_brightness_binding;
  hardware_slider_binding haptic_bindings[3];
  hardware_slider_binding recording_binding;
  bool quick_brightness_dirty = false;
  int screen_timeout_index = 3;
  int console_font_index = 1;
  bool include_kernel_log = false;
  bool include_logcat = true;
  lv_obj_t* kernel_log_card = nullptr;
  lv_obj_t* logcat_card = nullptr;
  lv_obj_t* export_result_label = nullptr;
  console_page_view console;
  wipe_progress_page_view wipe_progress;
  lv_obj_t* format_data_input = nullptr;
  lv_obj_t* format_data_button = nullptr;
  lv_obj_t* format_data_keyboard = nullptr;
  size_t wipe_console_consumed = 0;
  uint64_t wipe_last_poll_ms = 0;
  bool wipe_selected[24] = {};
  size_t wipe_target_count = 0;
  gui2_components::swipe_slider wipe_confirm;
  size_t console_consumed = 0;
  uint64_t console_last_poll_ms = 0;
  reboot_page_state reboot;
};

}  // namespace gui2_pages

#endif
