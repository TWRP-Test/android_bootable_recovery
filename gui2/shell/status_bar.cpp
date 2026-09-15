#include "shell/status_bar.h"

#include <algorithm>
#include <cstdio>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"
#include "gui2_svg_cache.h"

namespace gui2_shell {

namespace {

const lv_image_dsc_t* battery_icon_for(int percentage, bool charging) {
  const int clamped_percentage = std::clamp(percentage, 0, 100);
  if (clamped_percentage == 0) return charging ? &kGui2IconBattery0Charging : &kGui2IconBattery0;
  const int level = std::clamp(((clamped_percentage + 5) / 10) * 10, 10, 100);
  switch (level) {
    case 10:
      return charging ? &kGui2IconBattery10Charging : &kGui2IconBattery10;
    case 20:
      return charging ? &kGui2IconBattery20Charging : &kGui2IconBattery20;
    case 30:
      return charging ? &kGui2IconBattery30Charging : &kGui2IconBattery30;
    case 40:
      return charging ? &kGui2IconBattery40Charging : &kGui2IconBattery40;
    case 50:
      return charging ? &kGui2IconBattery50Charging : &kGui2IconBattery50;
    case 60:
      return charging ? &kGui2IconBattery60Charging : &kGui2IconBattery60;
    case 70:
      return charging ? &kGui2IconBattery70Charging : &kGui2IconBattery70;
    case 80:
      return charging ? &kGui2IconBattery80Charging : &kGui2IconBattery80;
    case 90:
      return charging ? &kGui2IconBattery90Charging : &kGui2IconBattery90;
    default:
      return charging ? &kGui2IconBattery100Charging : &kGui2IconBattery100;
  }
}

}  // namespace

status_bar_view create_status_bar(lv_obj_t* screen, const gui2_core::ui_metrics& metrics,
                                  const char* recording_text, lv_event_cb_t gesture_callback) {
  status_bar_view view;
  if (screen == nullptr) return view;

  view.root = lv_obj_create(screen);
  lv_obj_set_pos(view.root, 0, 0);
  lv_obj_set_size(view.root, metrics.width, metrics.status_height);
  gui2_core::set_surface_style(view.root, metrics.background);
  lv_obj_set_style_pad_all(view.root, 0, LV_PART_MAIN);
  lv_obj_add_flag(view.root, LV_OBJ_FLAG_CLICKABLE);
  if (gesture_callback != nullptr)
    lv_obj_add_event_cb(view.root, gesture_callback, LV_EVENT_ALL, nullptr);
  gui2_core::disable_scrolling(view.root);

  view.time_label = lv_label_create(view.root);
  lv_label_set_text(view.time_label, "");
  lv_obj_set_style_text_color(view.time_label, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.time_label, metrics.status_font, LV_PART_MAIN);

  view.battery_value = lv_label_create(view.root);
  lv_label_set_text(view.battery_value, "--%");
  lv_obj_set_style_text_color(view.battery_value, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.battery_value, metrics.status_font, LV_PART_MAIN);

  view.battery_icon = gui2_components::create_svg_image(view.root, &kGui2IconBattery100,
                                                        gui2_core::ui_px(72), gui2_core::ui_px(48));

  view.recording_indicator = lv_label_create(view.root);
  lv_label_set_text(view.recording_indicator, recording_text == nullptr ? "" : recording_text);
  lv_obj_set_style_text_color(view.recording_indicator, lv_color_hex(0xF0443E), LV_PART_MAIN);
  lv_obj_set_style_text_font(view.recording_indicator, metrics.status_font, LV_PART_MAIN);
  lv_obj_add_flag(view.recording_indicator, LV_OBJ_FLAG_HIDDEN);

  layout_status_bar(view, metrics);
  return view;
}

void layout_status_bar(const status_bar_view& view, const gui2_core::ui_metrics& metrics) {
  if (view.time_label == nullptr || metrics.status_font == nullptr) return;
  const int content_height = metrics.status_height - metrics.status_top_padding;
  const int y =
      metrics.status_top_padding + (content_height - metrics.status_font->line_height) / 2;
  lv_obj_align(view.time_label, LV_ALIGN_TOP_LEFT, metrics.outer_margin, y);
  if (view.recording_indicator != nullptr)
    lv_obj_align_to(view.recording_indicator, view.time_label, LV_ALIGN_OUT_RIGHT_MID,
                    gui2_core::ui_px(12), 0);
  if (view.battery_value != nullptr) {
    lv_obj_align(view.battery_value, LV_ALIGN_TOP_RIGHT, -metrics.outer_margin, y);
    if (view.battery_icon != nullptr)
      lv_obj_align_to(view.battery_icon, view.battery_value, LV_ALIGN_OUT_LEFT_MID,
                      -gui2_core::ui_px(12), 0);
  }
}

void update_status_bar(const status_bar_view& view, const gui2_core::ui_metrics& metrics,
                       const gui2_backend::status_snapshot& snapshot) {
  if (view.time_label != nullptr) lv_label_set_text(view.time_label, snapshot.time_text.c_str());
  if (view.battery_value == nullptr || view.battery_icon == nullptr) return;
  if (!snapshot.battery_valid) {
    lv_obj_add_flag(view.battery_value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view.battery_icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char battery_text[16];
  std::snprintf(battery_text, sizeof(battery_text), "%d%%",
                std::clamp(snapshot.battery_percentage, 0, 100));
  lv_label_set_text(view.battery_value, battery_text);
  lv_image_set_src(view.battery_icon,
                   gui2_svg_get_raster(
                       battery_icon_for(snapshot.battery_percentage, snapshot.charging),
                       lv_obj_get_width(view.battery_icon), lv_obj_get_height(view.battery_icon)));
  layout_status_bar(view, metrics);
  lv_obj_clear_flag(view.battery_value, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(view.battery_icon, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace gui2_shell
