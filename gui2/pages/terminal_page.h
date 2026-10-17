#ifndef GUI2_PAGES_TERMINAL_PAGE_H
#define GUI2_PAGES_TERMINAL_PAGE_H

#include "components/keyboard.h"
#include "pages/console_page.h"

namespace gui2_pages {

struct terminal_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const lv_font_t* console_font = nullptr;
  int top_offset = 0;
  gui2_components::keyboard* keyboard = nullptr;
  gui2_components::keyboard_callback key_callback = nullptr;
  gui2_components::keyboard_input_callback input_callback = nullptr;
  gui2_components::keyboard_action_callback action_callback = nullptr;
  gui2_components::keyboard_callback shown_callback = nullptr;
  gui2_components::keyboard_callback hidden_callback = nullptr;
  void* keyboard_user_data = nullptr;
};

struct terminal_page_view {
  lv_obj_t* body = nullptr;
  console_page_view output;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* cursor = nullptr;
  std::vector<lv_obj_t*> screen_lines;
  bool rendering = false;
  bool follow_output = true;
  bool alternate_screen = false;
  int cell_width = 1;
  int cell_height = 1;
  int columns = 1;
  int rows = 1;
};

void set_terminal_font(terminal_page_view* view, const lv_font_t* font);
void layout_terminal_output(terminal_page_view* view, const gui2_core::ui_metrics& metrics,
                            int top_offset, bool keyboard_visible);
terminal_page_view build_terminal_page(const terminal_page_options& options);
void render_terminal_screen(terminal_page_view* view, const std::vector<std::string>& lines,
                            int cursor_x, int cursor_y, bool alternate_screen);

}  // namespace gui2_pages
#endif
