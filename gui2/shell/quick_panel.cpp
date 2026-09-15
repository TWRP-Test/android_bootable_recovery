#include "shell/quick_panel.h"

#include <algorithm>

#include "components/icon.h"
#include "components/quick_action_button.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_shell {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

}  // namespace

quick_panel_view create_quick_panel(const quick_panel_options& options) {
  quick_panel_view view;
  if (options.metrics == nullptr) return view;
  const auto& metrics = *options.metrics;
  const int inner_padding = card_inner_padding(metrics);
  const int gap = metrics.card_gap;

  view.dismiss = lv_obj_create(lv_layer_top());
  lv_obj_set_size(view.dismiss, metrics.width, metrics.height);
  lv_obj_set_pos(view.dismiss, 0, 0);
  gui2_core::set_surface_style(view.dismiss, lv_color_hex(0x000000), LV_OPA_30);
  lv_obj_set_style_pad_all(view.dismiss, 0, LV_PART_MAIN);
  lv_obj_add_flag(view.dismiss, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(view.dismiss);
  if (options.press_guard_callback != nullptr)
    lv_obj_add_event_cb(view.dismiss, options.press_guard_callback, LV_EVENT_ALL, nullptr);
  if (options.dismiss_event_callback != nullptr)
    lv_obj_add_event_cb(view.dismiss, options.dismiss_event_callback, LV_EVENT_ALL, nullptr);

  view.menu = lv_obj_create(lv_layer_top());
  lv_obj_set_size(view.menu, metrics.content_width, 1);
  gui2_core::set_surface_style(view.menu, metrics.card_color);
  lv_obj_set_style_radius(view.menu, gui2_core::ui_px(28), LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.menu, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(view.menu, gui2_core::ui_px(12), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(view.menu, 48, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(view.menu, gui2_core::ui_px(4), LV_PART_MAIN);
  lv_obj_add_flag(view.menu, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(view.menu, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(view.menu);
  if (options.panel_gesture_callback != nullptr) {
    lv_obj_add_event_cb(view.menu, options.panel_gesture_callback, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(view.menu, options.panel_gesture_callback, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(view.menu, options.panel_gesture_callback, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(view.menu, options.panel_gesture_callback, LV_EVENT_PRESS_LOST, nullptr);
  }

  lv_obj_t* title = lv_label_create(view.menu);
  lv_label_set_text(title, options.title == nullptr ? "" : options.title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, inner_padding, inner_padding);
  lv_obj_set_style_text_color(title, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, metrics.text_font, LV_PART_MAIN);
  lv_obj_update_layout(title);
  int content_top = inner_padding + lv_obj_get_height(title) + gap;

  if (options.brightness_available && options.brightness_visual != nullptr &&
      options.brightness_value_label != nullptr) {
    const int header_height = std::max(1, metrics.status_font->line_height);
    const int slider_height = std::clamp(std::min(metrics.width, metrics.height) / 24,
                                         gui2_core::ui_px(28), gui2_core::ui_px(48));
    const int slider_gap =
        std::clamp(metrics.card_gap / 2, gui2_core::ui_px(8), gui2_core::ui_px(12));
    lv_obj_t* brightness_label = lv_label_create(view.menu);
    lv_label_set_text(brightness_label, options.brightness_label);
    lv_obj_set_pos(brightness_label, inner_padding, content_top);
    lv_obj_set_style_text_color(brightness_label, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness_label, metrics.status_font, LV_PART_MAIN);

    *options.brightness_value_label = lv_label_create(view.menu);
    lv_obj_set_width(*options.brightness_value_label, metrics.content_width - inner_padding * 2);
    lv_obj_set_style_text_align(*options.brightness_value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(*options.brightness_value_label, inner_padding, content_top);
    lv_obj_set_style_text_color(*options.brightness_value_label, metrics.secondary_text,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(*options.brightness_value_label, metrics.status_font, LV_PART_MAIN);

    const lv_color_t slider_background =
        lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 38);
    lv_obj_t* slider = gui2_components::create_slider(
        view.menu, inner_padding, content_top + header_height + slider_gap,
        metrics.content_width - inner_padding * 2, slider_height, 10, 100, options.brightness_value,
        slider_background, lv_color_hex(0x347FF1), lv_color_hex(0xFFFFFF),
        options.brightness_visual);
    if (slider != nullptr) {
      if (options.brightness_event_callback != nullptr) {
        lv_obj_add_event_cb(slider, options.brightness_event_callback, LV_EVENT_VALUE_CHANGED,
                            options.brightness_user_data);
        lv_obj_add_event_cb(slider, options.brightness_event_callback, LV_EVENT_RELEASED,
                            options.brightness_user_data);
        lv_obj_add_event_cb(slider, options.brightness_event_callback, LV_EVENT_PRESS_LOST,
                            options.brightness_user_data);
      }
      if (options.brightness_state_callback != nullptr) {
        lv_obj_add_event_cb(slider, options.brightness_state_callback, LV_EVENT_PRESSED,
                            options.brightness_user_data);
        lv_obj_add_event_cb(slider, options.brightness_state_callback, LV_EVENT_PRESSING,
                            options.brightness_user_data);
      }
    }
    content_top += header_height + slider_gap + slider_height + gap;
  }

  const int action_count = std::min<int>(3, options.action_count);
  const int action_height =
      std::clamp(metrics.height / 14, gui2_core::ui_px(124), gui2_core::ui_px(148));
  if (action_count > 0 && options.actions != nullptr) {
    const int columns = action_count;
    const int available_width = metrics.content_width - inner_padding * 2;
    const int button_width = (available_width - gap * (columns - 1)) / columns;
    const int row_width = button_width * action_count + gap * (action_count - 1);
    const int row_left = inner_padding + std::max(0, (available_width - row_width) / 2);
    for (int i = 0; i < action_count; ++i) {
      const int x = row_left + i * (button_width + gap);
      lv_obj_t* button = gui2_components::create_quick_action_button(
          view.menu, metrics, options.actions[i].icon, options.actions[i].text, x, content_top,
          button_width, options.actions[i].user_data, options.action_event_callback,
          options.press_guard_callback, options.panel_gesture_callback);
      if (options.actions[i].user_data == options.recording_action_user_data) {
        view.recording_button = button;
        view.recording_label = button == nullptr ? nullptr : lv_obj_get_child(button, 1);
      }
    }
    content_top += action_height;
  }

  view.feedback = lv_label_create(view.menu);
  const int feedback_height = std::max(gui2_core::ui_px(36), metrics.status_font->line_height * 2);
  const int feedback_gap =
      std::clamp(metrics.card_gap / 2, gui2_core::ui_px(8), gui2_core::ui_px(12));
  view.menu_height = content_top + feedback_gap + feedback_height + inner_padding;
  lv_obj_set_size(view.menu, metrics.content_width, view.menu_height);
  lv_obj_set_style_radius(
      view.menu, std::clamp(view.menu_height / 4, gui2_core::ui_px(24), gui2_core::ui_px(40)),
      LV_PART_MAIN);
  lv_obj_set_width(view.feedback, metrics.content_width - inner_padding * 2);
  lv_obj_set_height(view.feedback, feedback_height);
  lv_obj_set_pos(view.feedback, inner_padding, content_top + feedback_gap);
  lv_label_set_long_mode(view.feedback, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(view.feedback, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  lv_obj_set_style_text_color(view.feedback, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.feedback, metrics.status_font, LV_PART_MAIN);
  lv_obj_add_flag(view.feedback, LV_OBJ_FLAG_HIDDEN);
  gui2_core::disable_scrolling(view.feedback);

  view.menu_open_y = metrics.status_height + std::max(gui2_core::ui_px(8), metrics.card_gap / 2);
  view.menu_closed_y = -view.menu_height;
  lv_obj_set_pos(view.menu, metrics.outer_margin, view.menu_closed_y);

  view.screenshot_flash = lv_obj_create(lv_layer_top());
  lv_obj_set_size(view.screenshot_flash, metrics.width, metrics.height);
  lv_obj_set_pos(view.screenshot_flash, 0, 0);
  gui2_core::set_surface_style(view.screenshot_flash, lv_color_hex(0xFFFFFF), LV_OPA_30);
  lv_obj_set_style_pad_all(view.screenshot_flash, 0, LV_PART_MAIN);
  lv_obj_clear_flag(view.screenshot_flash, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(view.screenshot_flash);
  lv_obj_add_flag(view.screenshot_flash, LV_OBJ_FLAG_HIDDEN);
  return view;
}

}  // namespace gui2_shell
