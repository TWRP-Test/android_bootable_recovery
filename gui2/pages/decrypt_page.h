#ifndef GUI2_PAGES_DECRYPT_PAGE_H
#define GUI2_PAGES_DECRYPT_PAGE_H

#include "backend/decrypt_backend.h"
#include "components/pattern_lock.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct decrypt_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* page_layer = nullptr;
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  gui2_backend::lock_kind kind = gui2_backend::lock_kind::PASSWORD;
  bool failed = false;
  gui2_components::pattern_lock* pattern = nullptr;
  gui2_components::pattern_complete_callback pattern_callback = nullptr;
  gui2_components::pattern_dot_callback pattern_dot_callback = nullptr;
  void* pattern_user_data = nullptr;
  lv_event_cb_t input_ready_callback = nullptr;
  lv_event_cb_t keyboard_event_callback = nullptr;
  lv_event_cb_t language_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct decrypt_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* input = nullptr;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* status = nullptr;
};

decrypt_page_view build_decrypt_page(const decrypt_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_DECRYPT_PAGE_H
