#include "shell/confirm_dialog.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_shell {

lv_obj_t* create_confirm_dialog(const confirm_dialog_options& options) {
  if (options.metrics == nullptr) return nullptr;
  const auto& metrics = *options.metrics;
  const int width = std::min(metrics.content_width, gui2_core::ui_px(720));
  const int height =
      std::min(metrics.height - metrics.status_height - metrics.nav_height, gui2_core::ui_px(360));
  lv_obj_t* overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(overlay, metrics.width, metrics.height);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(overlay);

  lv_obj_t* card = lv_obj_create(overlay);
  lv_obj_set_size(card, width, height);
  lv_obj_center(card);
  gui2_core::set_surface_style(card, metrics.card_color);
  lv_obj_set_style_radius(card, std::max(gui2_core::ui_px(1), height / 8), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, gui2_core::card_inner_padding(), LV_PART_MAIN);
  gui2_core::disable_scrolling(card);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, options.title == nullptr ? "" : options.title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_color(title, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, metrics.text_font, LV_PART_MAIN);

  lv_obj_t* body = lv_label_create(card);
  lv_label_set_text(body, options.body == nullptr ? "" : options.body);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, width - gui2_core::card_inner_padding() * 2);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, metrics.text_font->line_height + gui2_core::ui_px(20));
  lv_obj_set_style_text_color(body, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(body, metrics.status_font, LV_PART_MAIN);

  const int button_width = (width - gui2_core::card_inner_padding() * 2 - metrics.card_gap) / 2;
  auto create_button = [&](const char* text, lv_color_t color, lv_color_t text_color,
                           lv_align_t align, lv_event_cb_t callback) {
    lv_obj_t* button = lv_obj_create(card);
    lv_obj_set_size(button, button_width, gui2_core::ui_px(82));
    lv_obj_align(button, align, 0, 0);
    gui2_core::set_surface_style(button, color);
    lv_obj_set_style_radius(button, gui2_core::ui_px(24), LV_PART_MAIN);
    gui2_core::disable_scrolling(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    if (options.press_guard_callback != nullptr)
      lv_obj_add_event_cb(button, options.press_guard_callback, LV_EVENT_ALL, nullptr);
    if (callback != nullptr) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, overlay);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text == nullptr ? "" : text);
    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
    lv_obj_center(label);
  };
  create_button(options.cancel, metrics.background, metrics.primary_text, LV_ALIGN_BOTTOM_LEFT,
                options.cancel_callback);
  create_button(options.confirm, lv_color_hex(0x347FF1), lv_color_hex(0xFFFFFF),
                LV_ALIGN_BOTTOM_RIGHT, options.confirm_callback);
  return overlay;
}

}  // namespace gui2_shell
