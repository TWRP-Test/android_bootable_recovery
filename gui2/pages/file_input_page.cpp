#include "pages/file_input_page.h"

#include <algorithm>

#include "components/section_label.h"
#include "components/setting_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

gui2_components::keyboard_layout layout_for(bool numeric) {
  return numeric ? gui2_components::keyboard_layout::NUMBER
                 : gui2_components::keyboard_layout::LETTERS;
}

}  // namespace

int file_input_bottom_reserved(const gui2_core::ui_metrics& metrics, bool numeric) {
  return gui2_components::keyboard_height(metrics, layout_for(numeric)) + metrics.cards_top_gap;
}

file_input_page_view build_file_input_page(const file_input_page_options& options) {
  file_input_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const int input_height = gui2_core::single_line_card_height() * 11 / 10;
  const int line_height = metrics.text_font != nullptr ? metrics.text_font->line_height : 0;
  const int input_pad = std::max(0, (input_height - line_height) / 2);

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.body);

  view.input = lv_textarea_create(view.body);
  lv_textarea_set_one_line(view.input, true);
  lv_textarea_set_placeholder_text(view.input,
                                   options.hint == nullptr ? "" : options.hint);
  if (options.initial_text != nullptr) lv_textarea_set_text(view.input, options.initial_text);
  lv_obj_set_size(view.input, metrics.content_width, input_height);
  gui2_core::set_surface_style(view.input, metrics.card_color);
  lv_obj_set_style_radius(view.input, input_height / 3, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.input, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(view.input, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.input, metrics.text_font, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.input, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(view.input, gui2_core::card_inner_padding(), LV_PART_MAIN);
  lv_obj_set_style_pad_top(view.input, input_pad, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(view.input, LV_SCROLLBAR_MODE_OFF);

  if (options.error_text != nullptr || options.note_text != nullptr ||
      options.action_title != nullptr)
    lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  if (options.error_text != nullptr) {
    lv_obj_t* error =
        gui2_components::create_section_label(view.body, metrics, options.error_text);
    lv_obj_set_style_text_color(error, lv_color_hex(0xF0443E), LV_PART_MAIN);
  }
  if (options.note_text != nullptr)
    gui2_components::create_section_label(view.body, metrics, options.note_text);
  if (options.action_title != nullptr)
    gui2_components::create_setting_card(view.body, metrics, options.action_title,
                                         options.action_detail, options.action_callback, nullptr,
                                         options.press_guard_callback);

  if (options.keyboard != nullptr) {
    gui2_components::keyboard_options keyboard;
    keyboard.parent = options.overlay_layer != nullptr ? options.overlay_layer : options.content;
    keyboard.metrics = &metrics;
    keyboard.strings = options.strings;
    keyboard.textarea = view.input;
    keyboard.layout = layout_for(options.numeric);
    keyboard.accept_callback = options.accept_callback;
    keyboard.key_callback = options.key_callback;
    keyboard.user_data = options.keyboard_user_data;
    view.keyboard = options.keyboard->create(keyboard);
    if (view.keyboard != nullptr && options.overlay_layer != nullptr) {
      lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(view.keyboard, 0,
                     metrics.height -
                         gui2_components::keyboard_height(metrics, layout_for(options.numeric)));
      options.keyboard->bind(view.input);
    }
  }
  return view;
}

}  // namespace gui2_pages
