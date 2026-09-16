#ifndef GUI2_PAGES_REBOOT_PAGE_H
#define GUI2_PAGES_REBOOT_PAGE_H

#include <cstddef>

#include "backend/reboot_backend.h"
#include "components/swipe_slider.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct reboot_option {
  gui2_backend::reboot_target target = gui2_backend::reboot_target::SYSTEM;
  const char* label = nullptr;
};

struct reboot_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;

  const reboot_option* options = nullptr;
  size_t option_count = 0;
  bool target_selected = false;
  gui2_backend::reboot_target selected_target = gui2_backend::reboot_target::SYSTEM;
  const char* error_text = nullptr;

  bool has_boot_slots = false;
  const char* current_slot_text = nullptr;
  const std::string* active_slot = nullptr;
  const gui2_backend::boot_slot* slots = nullptr;
  size_t slot_count = 0;

  lv_event_cb_t option_event_callback = nullptr;
  lv_event_cb_t slot_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  gui2_components::swipe_slider* confirmation_slider = nullptr;
  gui2_components::swipe_complete_callback confirmation_callback = nullptr;
  void* confirmation_user_data = nullptr;
};

struct reboot_page_view {
  lv_obj_t* body = nullptr;
};

reboot_page_view build_reboot_page(const reboot_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_REBOOT_PAGE_H
