#ifndef GUI2_PAGES_TERMINAL_PAGE_H
#define GUI2_PAGES_TERMINAL_PAGE_H

#include "components/keyboard.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/console_page.h"

namespace gui2_pages {

struct terminal_page_options {
  lv_obj_t* content = nullptr;
  // The keyboard lives above the page so it can cover the navigation.
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const lv_font_t* console_font = nullptr;
  const char* prompt = nullptr;
  // What sits above the pane, such as the tab bar.
  int top_offset = 0;
  gui2_components::keyboard* keyboard = nullptr;
  // Enter on the keyboard runs whatever is in the field.
  gui2_components::keyboard_callback run_callback = nullptr;
  gui2_components::keyboard_callback key_callback = nullptr;
  // The output box grows and shrinks with the keyboard.
  gui2_components::keyboard_callback shown_callback = nullptr;
  gui2_components::keyboard_callback hidden_callback = nullptr;
  void* keyboard_user_data = nullptr;
  lv_event_cb_t interrupt_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct terminal_page_view {
  lv_obj_t* body = nullptr;
  console_page_view output;
  lv_obj_t* prompt = nullptr;
  lv_obj_t* input = nullptr;
  lv_obj_t* keyboard = nullptr;
};

// Space the keyboard and the command row take at the bottom of the page.
int terminal_bottom_reserved(const gui2_core::ui_metrics& metrics);

// The output box takes whatever the keyboard is not using, so putting the
// keyboard away gives the terminal the same room the output tab has. Call it
// again every time the keyboard comes or goes.
void layout_terminal_output(terminal_page_view* view, const gui2_core::ui_metrics& metrics,
                            int top_offset, bool keyboard_visible);

terminal_page_view build_terminal_page(const terminal_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_TERMINAL_PAGE_H
