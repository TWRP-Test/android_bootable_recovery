#ifndef GUI2_PAGES_FILE_INPUT_PAGE_H
#define GUI2_PAGES_FILE_INPUT_PAGE_H

#include "components/keyboard.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// One field and a keyboard, which is all the legacy rename and chmod pages
// are. Which of the two it is only changes the keyboard and the starting text.
struct file_input_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const char* hint = nullptr;
  const char* initial_text = nullptr;
  bool numeric = false;
  gui2_components::keyboard* keyboard = nullptr;
  gui2_components::keyboard_callback accept_callback = nullptr;
  gui2_components::keyboard_callback key_callback = nullptr;
  void* keyboard_user_data = nullptr;
  // Under the field: a note, an error in red, and one card with an action.
  const char* note_text = nullptr;
  const char* error_text = nullptr;
  const char* action_title = nullptr;
  const char* action_detail = nullptr;
  lv_event_cb_t action_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct file_input_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* input = nullptr;
  lv_obj_t* keyboard = nullptr;
};

int file_input_bottom_reserved(const gui2_core::ui_metrics& metrics, bool numeric);

file_input_page_view build_file_input_page(const file_input_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_FILE_INPUT_PAGE_H
