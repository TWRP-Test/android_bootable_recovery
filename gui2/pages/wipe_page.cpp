#include "pages/wipe_page.h"

#include <algorithm>

#include "components/section_label.h"
#include "components/setting_card.h"
#include "components/check_row.h"
#include "components/icon.h"
#include "gui2_svg_assets.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

lv_obj_t* create_body(lv_obj_t* content, const gui2_core::ui_metrics& metrics) {
  lv_obj_t* body = lv_obj_create(content);
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  lv_obj_set_width(body, metrics.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(body, gui2_core::navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_add_flag(body, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  gui2_core::disable_scrolling(body);
  return body;
}

void show_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (keyboard != nullptr) lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void hide_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (keyboard != nullptr) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

int wipe_track_height() {
  return std::clamp(gui2_core::ui_px(150), gui2_core::ui_px(100), gui2_core::ui_px(180));
}

int wipe_hint_height(const gui2_core::ui_metrics& metrics, const char* text) {
  lv_point_t size;
  lv_text_get_size(&size, text == nullptr ? "" : text, metrics.status_font, 0, 0,
                   metrics.content_width, LV_TEXT_FLAG_NONE);
  return std::max<int32_t>(1, size.y);
}

int format_data_keyboard_height(const gui2_core::ui_metrics& metrics) {
  return std::min(metrics.height / 3, metrics.width * 2 / 3);
}

void build_wipe_page(const wipe_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  gui2_components::create_setting_card(options.content, metrics, strings.advanced_wipe_title,
                                       strings.advanced_wipe_summary,
                                       options.option_event_callback, options.advanced_target,
                                       options.press_guard_callback);
  if (options.has_data_media) {
    gui2_components::create_setting_card(options.content, metrics, strings.format_data_title,
                                         strings.format_data_summary,
                                         options.option_event_callback,
                                         options.format_data_target,
                                         options.press_guard_callback);
  }

  if (options.page_layer == nullptr) return;

  const int track_height = wipe_track_height();
  const int hint_height = wipe_hint_height(metrics, strings.factory_reset_detail);
  const int page_height = metrics.height - metrics.status_height - metrics.nav_height;
  const int track_y = page_height - track_height - metrics.cards_top_gap;

  lv_obj_t* hint = gui2_components::create_section_label(options.page_layer, metrics,
                                                         strings.factory_reset_detail);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_size(hint, metrics.content_width, hint_height);
  lv_obj_set_pos(hint, metrics.outer_margin, track_y - metrics.cards_top_gap - hint_height);

  if (options.confirm != nullptr) {
    options.confirm->create(options.page_layer, metrics, metrics.outer_margin, track_y,
                            metrics.content_width, track_height, strings.swipe_factory_reset,
                            options.confirm_callback, options.confirm_user_data);
  }
}

void build_advanced_wipe_page(const advanced_wipe_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.targets == nullptr || options.selected == nullptr ||
      options.target_indices == nullptr)
    return;

  const auto& metrics = *options.metrics;
  lv_obj_t* body = create_body(options.content, metrics);

  for (size_t i = 0; i < options.target_count; ++i) {
    const char* name = options.targets[i].mount_point == "DALVIK"
                           ? options.strings->dalvik_cache
                           : options.targets[i].name.c_str();
    gui2_components::create_check_row(
        body, metrics, name, options.selected[i], options.option_event_callback,
        const_cast<void*>(static_cast<const void*>(&options.target_indices[i])));
  }
}

format_data_page_view build_format_data_page(const format_data_page_options& options) {
  format_data_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  view.body = create_body(options.content, metrics);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  const int warning_pad = gui2_core::card_inner_padding();
  lv_obj_t* warning_card = lv_obj_create(view.body);
  lv_obj_set_width(warning_card, metrics.content_width);
  lv_obj_set_height(warning_card, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(warning_card, lv_color_hex(0x2A1010));
  lv_obj_set_style_radius(warning_card, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(warning_card, warning_pad, LV_PART_MAIN);
  lv_obj_set_style_border_width(warning_card, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(warning_card);

  lv_obj_t* warning = lv_label_create(warning_card);
  lv_label_set_text(warning, strings.format_data_warning);
  lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(warning, std::max(1, metrics.content_width - warning_pad * 2));
  lv_obj_set_style_text_color(warning, lv_color_hex(0xF0443E), LV_PART_MAIN);
  lv_obj_set_style_text_font(warning, metrics.status_font, LV_PART_MAIN);

  const int icon_box = std::clamp(metrics.content_width / 2, gui2_core::ui_px(220),
                                  gui2_core::ui_px(340));
  lv_obj_t* icon_holder = lv_obj_create(view.body);
  lv_obj_set_size(icon_holder, icon_box, icon_box);
  gui2_core::set_surface_style(icon_holder, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(icon_holder, 0, LV_PART_MAIN);
  // Keeps the triangle clear of the warning above and the prompt below.
  lv_obj_set_style_margin_top(icon_holder, metrics.card_gap * 2, LV_PART_MAIN);
  lv_obj_set_style_margin_bottom(icon_holder, metrics.card_gap * 2, LV_PART_MAIN);
  gui2_core::disable_scrolling(icon_holder);

  lv_obj_t* icon = gui2_components::create_svg_image(icon_holder, &kGui2IconWarning, icon_box,
                                                     icon_box);
  lv_obj_center(icon);

  lv_obj_t* prompt =
      gui2_components::create_section_label(view.body, metrics, strings.format_data_prompt);
  lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  const int input_height = gui2_core::single_line_card_height() * 11 / 10;
  const int input_text_pad =
      std::max(0, (input_height - metrics.text_font->line_height) / 2);
  view.input = lv_textarea_create(view.body);
  lv_textarea_set_one_line(view.input, true);
  lv_obj_set_size(view.input, metrics.content_width, input_height);
  lv_obj_set_style_pad_top(view.input, input_text_pad, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(view.input, input_text_pad, LV_PART_MAIN);
  lv_textarea_set_max_length(view.input, 8);
  lv_obj_set_scrollbar_mode(view.input, LV_SCROLLBAR_MODE_OFF);
  gui2_core::set_surface_style(view.input, metrics.card_color);
  lv_obj_set_style_radius(view.input, input_height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.input, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(view.input, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.input, metrics.text_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(view.input, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  if (options.input_event_callback != nullptr)
    lv_obj_add_event_cb(view.input, options.input_event_callback, LV_EVENT_VALUE_CHANGED, nullptr);

  const int keyboard_height = format_data_keyboard_height(metrics);
  lv_obj_t* keyboard =
      lv_keyboard_create(options.overlay_layer != nullptr ? options.overlay_layer : view.body);
  lv_obj_set_size(keyboard, metrics.width, keyboard_height);
  if (options.overlay_layer != nullptr) {
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 0, metrics.height - keyboard_height);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(view.input, show_keyboard_cb, LV_EVENT_CLICKED, keyboard);
    lv_obj_add_event_cb(keyboard, hide_keyboard_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, hide_keyboard_cb, LV_EVENT_CANCEL, nullptr);
  }
  if (options.keyboard_event_callback != nullptr)
    lv_obj_add_event_cb(keyboard, options.keyboard_event_callback, LV_EVENT_PRESSED, nullptr);
  lv_obj_set_style_radius(keyboard, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(keyboard, gui2_core::ui_px(72), LV_PART_MAIN);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(keyboard, view.input);
  lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_48, LV_PART_ITEMS);
  view.keyboard = keyboard;

  if (options.confirm != nullptr && options.page_layer != nullptr) {
    const int track_height = wipe_track_height();
    const int page_height = metrics.height - metrics.status_height - metrics.nav_height;
    view.slider_track = options.confirm->create(
        options.page_layer, metrics, metrics.outer_margin,
        page_height - track_height - metrics.cards_top_gap, metrics.content_width, track_height,
        strings.swipe_format_data, options.confirm_callback, options.confirm_user_data);
  }
  return view;
}

}  // namespace gui2_pages
