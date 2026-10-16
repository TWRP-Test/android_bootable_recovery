#include "pages/terminal_page.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_pages {

void set_terminal_font(terminal_page_view* view, const lv_font_t* font) {
  if (view == nullptr || font == nullptr) return;
  view->output.font = font;
  lv_font_glyph_dsc_t glyph{};
  if (lv_font_get_glyph_dsc(font, &glyph, 'M', 0))
    view->cell_width = std::max(1, static_cast<int>(glyph.adv_w));
  view->cell_height = std::max(1, static_cast<int>(font->line_height));
  for (auto* line : view->screen_lines) {
    lv_obj_set_style_text_font(line, font, LV_PART_MAIN);
    lv_obj_invalidate(line);
  }
  if (view->output.body != nullptr) lv_obj_invalidate(view->output.body);
}

void layout_terminal_output(terminal_page_view* view, const gui2_core::ui_metrics& metrics,
                            int top_offset, bool keyboard_visible) {
  if (view == nullptr || view->output.body == nullptr) return;
  lv_obj_t* content = lv_obj_get_parent(view->body);
  lv_obj_update_layout(content);
  lv_area_t area;
  lv_obj_get_coords(content, &area);
  const int bottom = keyboard_visible ?
      metrics.height - gui2_components::keyboard_height(
          metrics, gui2_components::keyboard_layout::LETTERS, true) - metrics.cards_top_gap :
      metrics.height - gui2_core::navigation_safe_area();
  const int height = std::max(view->cell_height + view->output.padding * 2,
                              bottom - area.y1 - top_offset);
  lv_obj_set_height(view->output.body, height);
  view->output.minimum_height = height;
  view->rows = std::max(1, (height - view->output.padding * 2) / view->cell_height);
  view->columns = std::max(1, (lv_obj_get_width(view->output.body) -
                              view->output.padding * 2) / view->cell_width);
}

terminal_page_view build_terminal_page(const terminal_page_options& options) {
  terminal_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;
  const auto& metrics = *options.metrics;
  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.content_left, options.top_offset);
  lv_obj_set_size(view.body, metrics.content_width, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.cards_top_gap, LV_PART_MAIN);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  gui2_core::disable_scrolling(view.body);

  view.output.body = lv_obj_create(view.body);
  lv_obj_set_width(view.output.body, metrics.content_width);
  gui2_core::set_surface_style(view.output.body, metrics.card_color);
  lv_obj_set_style_radius(view.output.body, gui2_core::single_line_card_height() / 4, 0);
  lv_obj_set_style_clip_corner(view.output.body, true, 0);
  view.output.padding = std::max(gui2_core::ui_px(12), gui2_core::card_inner_padding() / 2);
  lv_obj_set_style_pad_all(view.output.body, view.output.padding, 0);
  view.output.content = view.output.body;
  view.output.self_scrolling = true;
  set_terminal_font(&view, options.console_font);
  lv_obj_set_scroll_dir(view.output.body, LV_DIR_VER);
  lv_obj_set_scroll_chain(view.output.body, false);
  lv_obj_set_scroll_elastic(view.output.body, false);
  gui2_core::style_scrollbar(view.output.body, metrics.secondary_text);

  if (options.keyboard != nullptr) {
    gui2_components::keyboard_options keyboard;
    keyboard.parent = options.overlay_layer != nullptr ? options.overlay_layer : options.content;
    keyboard.metrics = &metrics;
    keyboard.strings = options.strings;
    keyboard.shell_row = true;
    keyboard.input_callback = options.input_callback;
    keyboard.action_callback = options.action_callback;
    keyboard.key_callback = options.key_callback;
    keyboard.shown_callback = options.shown_callback;
    keyboard.hidden_callback = options.hidden_callback;
    keyboard.user_data = options.keyboard_user_data;
    view.keyboard = options.keyboard->create(keyboard);
    if (view.keyboard != nullptr && options.overlay_layer != nullptr) {
      lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(view.keyboard, 0, metrics.height - gui2_components::keyboard_height(
          metrics, gui2_components::keyboard_layout::LETTERS, true));
    }
  }
  layout_terminal_output(&view, metrics, options.top_offset,
                         options.keyboard != nullptr && options.keyboard->visible());
  return view;
}

void render_terminal_screen(terminal_page_view* view, const std::vector<std::string>& lines,
                            int cursor_x, int cursor_y, bool alternate_screen) {
  if (view == nullptr || view->output.body == nullptr) return;
  view->rendering = true;
  if (view->alternate_screen != alternate_screen) {
    view->alternate_screen = alternate_screen;
    view->follow_output = true;
    lv_obj_set_scrollable(view->output.body, !alternate_screen);
    lv_obj_scroll_to_y(view->output.body, 0, LV_ANIM_OFF);
  }
  const size_t count = alternate_screen ? static_cast<size_t>(view->rows) : lines.size();
  while (view->screen_lines.size() > count) {
    lv_obj_delete(view->screen_lines.back());
    view->screen_lines.pop_back();
  }
  while (view->screen_lines.size() < count) {
    auto* line = lv_label_create(view->output.body);
    lv_label_set_long_mode(line, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(line, view->output.font, 0);
    lv_obj_set_style_text_color(line, gui2_core::ui.primary_text, 0);
    lv_obj_set_style_text_letter_space(line, 0, 0);
    lv_obj_set_style_text_line_space(line, 0, 0);
    lv_obj_set_clickable(line, false);
    view->screen_lines.push_back(line);
  }
  for (size_t i = 0; i < count; ++i) {
    auto* line = view->screen_lines[i];
    const char* text = i < lines.size() ? lines[i].c_str() : "";
    if (std::string(lv_label_get_text(line)) != text) lv_label_set_text(line, text);
    lv_obj_set_pos(line, 0, static_cast<int>(i) * view->cell_height);
    lv_obj_set_size(line, view->columns * view->cell_width, view->cell_height);
  }
  if (view->cursor == nullptr) {
    view->cursor = lv_obj_create(view->output.body);
    gui2_core::set_surface_style(view->cursor, gui2_core::ui.accent);
    lv_obj_set_style_pad_all(view->cursor, 0, 0);
    lv_obj_set_style_border_width(view->cursor, 0, 0);
    gui2_core::disable_scrolling(view->cursor);
    lv_obj_set_clickable(view->cursor, false);
  }
  const bool cursor_visible = cursor_y >= 0 && cursor_y < static_cast<int>(count);
  lv_obj_set_hidden(view->cursor, !cursor_visible);
  if (cursor_visible) {
    lv_obj_set_size(view->cursor, view->cell_width, std::max(2, gui2_core::ui_px(3)));
    lv_obj_set_pos(view->cursor, std::clamp(cursor_x, 0, view->columns - 1) * view->cell_width,
                   (cursor_y + 1) * view->cell_height - gui2_core::ui_px(3));
    lv_obj_move_foreground(view->cursor);
  }
  lv_obj_update_layout(view->output.body);
  if (!alternate_screen && view->follow_output)
    lv_obj_scroll_to_y(view->output.body, LV_COORD_MAX, LV_ANIM_OFF);
  view->rendering = false;
}

}  // namespace gui2_pages
