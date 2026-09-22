#include "pages/terminal_page.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;
constexpr uint32_t kDanger = 0xF0443E;

int command_row_height(const gui2_core::ui_metrics& metrics) {
  return gui2_core::single_line_card_height() * 11 / 10;
}

}  // namespace

// Only the keyboard sits below the content; the command row is part of it.
// The lift has to be counted: the keyboard is drawn that much higher up the
// screen, so leaving it out is what let the keyboard cover the command row.
int terminal_bottom_reserved(const gui2_core::ui_metrics& metrics) {
  return gui2_components::keyboard_height(metrics, gui2_components::keyboard_layout::LETTERS,
                                          true) +
         metrics.cards_top_gap;
}

void layout_terminal_output(terminal_page_view* view, const gui2_core::ui_metrics& metrics,
                            int top_offset, bool keyboard_visible) {
  if (view == nullptr || view->output.body == nullptr) return;

  // The console sizes itself against the whole viewport and knows nothing about
  // the command row or the keyboard below it.
  const int scroll_top = metrics.heading_top + metrics.heading_height + metrics.cards_top_gap;
  const int reserved = keyboard_visible ? terminal_bottom_reserved(metrics)
                                        : gui2_core::navigation_safe_area();
  const int available = metrics.height - metrics.status_height - scroll_top - reserved - top_offset;
  const int height =
      std::max(gui2_core::ui_px(240), available - command_row_height(metrics) -
                                          metrics.cards_top_gap);
  lv_obj_set_height(view->output.body, height);
  view->output.minimum_height = height;
}

terminal_page_view build_terminal_page(const terminal_page_options& options) {
  terminal_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  const int row_height = command_row_height(metrics);

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, options.top_offset);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.cards_top_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.body);

  console_page_options output;
  output.content = view.body;
  output.metrics = &metrics;
  output.empty_text = strings.terminal_empty;
  output.font = options.console_font;
  output.self_scrolling = true;
  view.output = build_console_page(output);
  if (view.output.body != nullptr) lv_obj_set_pos(view.output.body, 0, 0);
  layout_terminal_output(&view, metrics, options.top_offset, true);

  // The command row sits where the legacy page puts its input line: directly
  // under the output, with the prompt in front of it.
  lv_obj_t* row = lv_obj_create(view.body);
  lv_obj_set_size(row, metrics.content_width, row_height);
  gui2_core::set_surface_style(row, metrics.card_color);
  lv_obj_set_style_radius(row, row_height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(row, gui2_core::card_inner_padding(), LV_PART_MAIN);
  lv_obj_set_style_pad_right(row, gui2_core::card_inner_padding() / 2, LV_PART_MAIN);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  gui2_core::disable_scrolling(row);

  view.prompt = lv_label_create(row);
  lv_label_set_text(view.prompt, options.prompt == nullptr ? "#" : options.prompt);
  lv_obj_set_style_text_color(view.prompt, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_text_font(view.prompt, options.console_font, LV_PART_MAIN);

  const int interrupt_width = row_height * 9 / 10;
  view.input = lv_textarea_create(row);
  lv_textarea_set_one_line(view.input, true);
  lv_textarea_set_placeholder_text(view.input, strings.terminal_hint);
  lv_obj_set_height(view.input, row_height);
  lv_obj_set_flex_grow(view.input, 1);
  gui2_core::set_surface_style(view.input, metrics.card_color, LV_OPA_TRANSP);
  lv_obj_set_style_border_width(view.input, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(view.input, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.input, options.console_font, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.input, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(view.input, gui2_core::ui_px(10), LV_PART_MAIN);
  // A one line field still puts its text at the top; pad it onto the same
  // baseline as the prompt beside it.
  const int line_height = options.console_font != nullptr ? options.console_font->line_height : 0;
  lv_obj_set_style_pad_top(view.input, std::max(0, (row_height - line_height) / 2), LV_PART_MAIN);

  // Ctrl-C has nowhere else to live on a keyboard built from letters.
  lv_obj_t* interrupt = lv_obj_create(row);
  lv_obj_set_size(interrupt, interrupt_width, interrupt_width);
  lv_obj_add_flag(interrupt, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::set_surface_style(interrupt, lv_color_hex(kDanger));
  lv_obj_set_style_radius(interrupt, interrupt_width / 3, LV_PART_MAIN);
  lv_obj_set_style_border_width(interrupt, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(interrupt, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(interrupt);
  if (options.press_guard_callback != nullptr)
    lv_obj_add_event_cb(interrupt, options.press_guard_callback, LV_EVENT_ALL, nullptr);
  if (options.interrupt_callback != nullptr)
    lv_obj_add_event_cb(interrupt, options.interrupt_callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* interrupt_label = lv_label_create(interrupt);
  lv_label_set_text(interrupt_label, "^C");
  lv_obj_set_style_text_color(interrupt_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(interrupt_label, options.console_font, LV_PART_MAIN);
  lv_obj_center(interrupt_label);

  if (options.keyboard != nullptr) {
    gui2_components::keyboard_options keyboard;
    keyboard.parent = options.overlay_layer != nullptr ? options.overlay_layer : options.content;
    keyboard.metrics = &metrics;
    keyboard.strings = &strings;
    keyboard.textarea = view.input;
    keyboard.layout = gui2_components::keyboard_layout::LETTERS;
    keyboard.shell_row = true;
    keyboard.accept_callback = options.run_callback;
    keyboard.key_callback = options.key_callback;
    keyboard.shown_callback = options.shown_callback;
    keyboard.hidden_callback = options.hidden_callback;
    keyboard.user_data = options.keyboard_user_data;
    view.keyboard = options.keyboard->create(keyboard);
    if (view.keyboard != nullptr && options.overlay_layer != nullptr) {
      lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(view.keyboard, 0,
                     metrics.height -
                         gui2_components::keyboard_height(
                             metrics, gui2_components::keyboard_layout::LETTERS, true));
      options.keyboard->bind(view.input);
    }
  }
  return view;
}

}  // namespace gui2_pages
