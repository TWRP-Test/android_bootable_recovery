#include "pages/console_page.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

lv_color_t severity_color(const gui2_core::ui_metrics& metrics,
                          gui2_backend::console_severity severity) {
  switch (severity) {
    case gui2_backend::console_severity::ERROR:
      return lv_color_hex(0xF0443E);
    case gui2_backend::console_severity::WARNING:
      return lv_color_hex(0xFFAA20);
    case gui2_backend::console_severity::HIGHLIGHT:
      return lv_color_hex(0x9BC5E9);
    case gui2_backend::console_severity::NORMAL:
      break;
  }
  return metrics.primary_text;
}

}  // namespace

console_page_view build_console_page(const console_page_options& options) {
  console_page_view view;
  if (options.content == nullptr || options.metrics == nullptr) return view;

  const auto& metrics = *options.metrics;
  view.content = options.content;
  view.font = options.font != nullptr ? options.font : metrics.status_font;

  view.padding = std::max(gui2_core::ui_px(12), gui2_core::card_inner_padding() / 2);
  view.line_gap = gui2_core::ui_px(8);
  view.next_y = 0;

  const int scroll_top = metrics.heading_top + metrics.heading_height + metrics.cards_top_gap;
  const int viewport_height = metrics.height - metrics.status_height - scroll_top;
  view.minimum_height =
      std::max(gui2_core::ui_px(400), viewport_height - gui2_core::navigation_safe_area());

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_size(view.body, metrics.content_width, view.minimum_height);
  gui2_core::set_surface_style(view.body, metrics.card_color);
  lv_obj_set_style_radius(view.body, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  // Real padding rather than an offset on every line: LVGL counts padding in
  // the scroll range, so the text keeps its margin at both ends instead of
  // ending flush against the edge. The scrollbar is a separate part and stays
  // on the full height.
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(view.body, view.padding, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(view.body, view.padding, LV_PART_MAIN);
  view.self_scrolling = options.self_scrolling;
  if (view.self_scrolling) {
    lv_obj_add_flag(view.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(view.body, LV_DIR_VER);
    gui2_core::style_scrollbar(view.body, metrics.secondary_text);
    view.content = view.body;
  } else {
    gui2_core::disable_scrolling(view.body);
  }

  view.empty_label = lv_label_create(view.body);
  lv_label_set_text(view.empty_label, options.empty_text == nullptr ? "" : options.empty_text);
  lv_label_set_long_mode(view.empty_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(view.empty_label, std::max(1, metrics.content_width - view.padding * 2));
  lv_obj_set_pos(view.empty_label, view.padding, 0);
  lv_obj_set_style_text_color(view.empty_label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.empty_label, view.font, LV_PART_MAIN);
  return view;
}

void append_console_lines(console_page_view* view, const gui2_core::ui_metrics& metrics,
                          const std::vector<gui2_backend::console_line>& lines) {
  if (view == nullptr || view->body == nullptr || lines.empty()) return;

  if (view->empty_label != nullptr) {
    lv_obj_delete(view->empty_label);
    view->empty_label = nullptr;
  }

  const int text_width = std::max(1, metrics.content_width - view->padding * 2);
  const lv_font_t* font = view->font != nullptr ? view->font : metrics.status_font;
  for (const auto& line : lines) {
    lv_obj_t* label = lv_label_create(view->body);
    lv_label_set_text(label, line.text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, severity_color(metrics, line.severity), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);

    const int32_t letter_space = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    const int32_t line_space = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, line.text.c_str(), font, letter_space, line_space, text_width,
                     LV_TEXT_FLAG_NONE);

    lv_obj_set_size(label, text_width, std::max<int32_t>(1, size.y));
    lv_obj_set_pos(label, view->padding, view->next_y);  // y is inside the padding
    view->next_y += std::max<int32_t>(1, size.y) + view->line_gap;
  }

  if (view->self_scrolling) return;

  const int height = std::max(view->minimum_height,
                              view->next_y - view->line_gap + view->padding * 2 +
                                  gui2_core::navigation_safe_area());
  lv_obj_set_height(view->body, height);
}

void drop_last_console_line(console_page_view* view) {
  if (view == nullptr || view->body == nullptr) return;
  const uint32_t count = lv_obj_get_child_count(view->body);
  if (count == 0) return;

  lv_obj_t* last = lv_obj_get_child(view->body, count - 1);
  if (last == nullptr) return;
  view->next_y -= lv_obj_get_height(last) + view->line_gap;
  if (view->next_y < 0) view->next_y = 0;
  lv_obj_delete(last);
}

void clear_console_lines(console_page_view* view) {
  if (view == nullptr || view->body == nullptr) return;
  lv_obj_clean(view->body);
  view->empty_label = nullptr;
  view->next_y = 0;
}

void scroll_console_to_end(const console_page_view& view) {
  if (view.content == nullptr) return;
  lv_obj_scroll_to_y(view.content, lv_obj_get_scroll_bottom(view.content) +
                                       lv_obj_get_scroll_y(view.content),
                     LV_ANIM_OFF);
}

}  // namespace gui2_pages
