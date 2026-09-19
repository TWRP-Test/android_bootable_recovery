#include "pages/decrypt_page.h"

#include <algorithm>

#include "components/apply_button.h"
#include "components/icon.h"
#include "components/keyboard_maps.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;

void show_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (keyboard != nullptr) lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void hide_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (keyboard != nullptr) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

decrypt_page_view build_decrypt_page(const decrypt_page_options& options) {
  decrypt_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  const bool pattern_mode = options.kind == gui2_backend::lock_kind::PATTERN;

  const int viewport = std::max(gui2_core::ui_px(400),
                                metrics.height - metrics.status_height - metrics.heading_top -
                                    metrics.heading_height - metrics.cards_top_gap -
                                    metrics.nav_height - gui2_core::single_line_card_height() -
                                    metrics.cards_top_gap * 2);
  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, viewport);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  // Top aligned on purpose: the badge and its prompt must land at the same
  // height whether the grid or the keypad follows them.
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(view.body, metrics.cards_top_gap * 2, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.body);

  const int badge =
      std::clamp(metrics.icon_size * 3 / 2, gui2_core::ui_px(140), gui2_core::ui_px(200));
  lv_obj_t* icon = lv_obj_create(view.body);
  lv_obj_set_size(icon, badge, badge);
  lv_obj_set_style_radius(icon, badge / 4, LV_PART_MAIN);
  gui2_core::set_surface_style(icon, lv_color_hex(kAccent));
  lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(icon);
  lv_obj_t* art = gui2_components::create_svg_image(
      icon, &kGui2IconLock, gui2_components::action_icon_art_size(badge),
      gui2_components::action_icon_art_size(badge));
  lv_obj_center(art);

  view.status = lv_label_create(view.body);
  lv_obj_set_style_margin_top(view.status, metrics.card_gap * 2, LV_PART_MAIN);
  lv_label_set_text(view.status, options.failed ? strings.decrypt_failed
                                                : (pattern_mode ? strings.decrypt_pattern_prompt
                                                                : strings.decrypt_password_prompt));
  lv_label_set_long_mode(view.status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(view.status, metrics.content_width);
  lv_obj_set_style_text_align(view.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.status, metrics.status_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(
      view.status, lv_color_hex(options.failed ? 0xF0443E : kAccent), LV_PART_MAIN);

  if (pattern_mode) {
    if (options.pattern != nullptr) {
      const int grid = std::min(metrics.content_width, metrics.height * 2 / 5);
      lv_obj_t* holder = lv_obj_create(view.body);
      lv_obj_set_size(holder, grid, grid);
      lv_obj_set_style_margin_top(holder, metrics.card_gap * 3, LV_PART_MAIN);
      gui2_core::set_surface_style(holder, metrics.background, LV_OPA_TRANSP);
      lv_obj_set_style_pad_all(holder, 0, LV_PART_MAIN);
      gui2_core::disable_scrolling(holder);
      options.pattern->create(holder, metrics, 0, 0, grid, options.pattern_callback,
                              options.pattern_user_data, options.pattern_dot_callback);
    }
  } else {
    const int input_height = gui2_core::single_line_card_height() * 11 / 10;
    const int input_pad = std::max(0, (input_height - metrics.text_font->line_height) / 2);
    view.input = lv_textarea_create(view.body);
    lv_textarea_set_one_line(view.input, true);
    lv_obj_set_size(view.input, metrics.content_width, input_height);
    lv_obj_set_style_pad_top(view.input, input_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(view.input, input_pad, LV_PART_MAIN);
    lv_obj_set_style_margin_top(view.input, metrics.card_gap * 2, LV_PART_MAIN);
    lv_textarea_set_password_mode(view.input, true);
    lv_obj_set_scrollbar_mode(view.input, LV_SCROLLBAR_MODE_OFF);
    gui2_core::set_surface_style(view.input, metrics.card_color);
    lv_obj_set_style_radius(view.input, input_height / 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(view.input, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(view.input, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(view.input, metrics.text_font, LV_PART_MAIN);
    lv_obj_set_style_text_align(view.input, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    const int keyboard_height =
        std::min(metrics.height / 3, metrics.width * 2 / 3);
    lv_obj_t* parent = options.overlay_layer != nullptr ? options.overlay_layer : view.body;
    view.keyboard = lv_keyboard_create(parent);
    gui2_components::install_keyboard_maps(view.keyboard);
    lv_obj_set_size(view.keyboard, metrics.width, keyboard_height);
    lv_keyboard_set_mode(view.keyboard, options.kind == gui2_backend::lock_kind::PIN
                                            ? LV_KEYBOARD_MODE_NUMBER
                                            : LV_KEYBOARD_MODE_TEXT_LOWER);
    if (options.overlay_layer != nullptr) {
      lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(view.keyboard, 0, metrics.height - keyboard_height);
      lv_obj_add_event_cb(view.input, show_keyboard_cb, LV_EVENT_CLICKED, view.keyboard);
    }
    lv_obj_set_style_radius(view.keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(view.keyboard, gui2_core::ui_px(72), LV_PART_MAIN);
    lv_keyboard_set_textarea(view.keyboard, view.input);
    lv_obj_set_style_text_font(view.keyboard, &lv_font_montserrat_48, LV_PART_ITEMS);
    lv_obj_add_event_cb(view.keyboard, hide_keyboard_cb, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(view.keyboard, hide_keyboard_cb, LV_EVENT_READY, nullptr);
    if (options.input_ready_callback != nullptr)
      lv_obj_add_event_cb(view.keyboard, options.input_ready_callback, LV_EVENT_READY, nullptr);
    if (options.keyboard_event_callback != nullptr)
      lv_obj_add_event_cb(view.keyboard, options.keyboard_event_callback, LV_EVENT_PRESSED,
                          nullptr);
  }

  if (options.language_callback != nullptr && options.page_layer != nullptr) {
    gui2_components::create_apply_button(options.page_layer, metrics, options.language_callback,
                                         strings.change_language, options.press_guard_callback);
  }
  return view;
}

}  // namespace gui2_pages
