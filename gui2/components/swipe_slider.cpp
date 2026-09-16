#include "components/swipe_slider.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_components {

lv_obj_t* swipe_slider::create(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int x, int y,
                               int width, int height, const char* text,
                               swipe_complete_callback callback, void* user_data) {
  callback_ = callback;
  user_data_ = user_data;
  track_ = lv_obj_create(parent);
  lv_obj_set_size(track_, width, height);
  lv_obj_set_pos(track_, x, y);
  gui2_core::set_surface_style(track_, lv_color_hex(0x454545));
  lv_obj_set_style_radius(track_, height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(track_, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(track_);
  lv_obj_add_flag(track_, LV_OBJ_FLAG_CLICKABLE);

  inner_margin_ = std::clamp(height / 10, gui2_core::ui_px(10), gui2_core::ui_px(22));
  const int inner_height = height - inner_margin_ * 2;
  const int inner_width = std::max(1, width - inner_margin_ * 2);
  knob_width_ = std::clamp(inner_height * 22 / 10, gui2_core::ui_px(200),
                           std::max(gui2_core::ui_px(200), inner_width * 38 / 100));
  prompt_inset_ = gui2_core::ui_px(96);

  prompt_ = lv_label_create(track_);
  lv_label_set_text(prompt_, text == nullptr ? "" : text);
  lv_obj_set_width(prompt_, std::max(1, inner_width - knob_width_ + prompt_inset_));
  lv_obj_set_style_text_align(prompt_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(prompt_, lv_color_hex(0xBDBDBD), LV_PART_MAIN);
  lv_obj_set_style_text_font(prompt_, metrics.text_font, LV_PART_MAIN);
  lv_obj_update_layout(prompt_);
  lv_obj_set_pos(prompt_, inner_margin_ + knob_width_ - prompt_inset_,
                 (height - lv_obj_get_height(prompt_)) / 2);
  lv_obj_clear_flag(prompt_, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(prompt_);

  fill_ = lv_obj_create(track_);
  lv_obj_set_size(fill_, knob_width_, inner_height);
  lv_obj_set_pos(fill_, inner_margin_, inner_margin_);
  gui2_core::set_surface_style(fill_, lv_color_hex(0x9BC5E9));
  lv_obj_set_style_radius(fill_, inner_height / 2, LV_PART_MAIN);
  lv_obj_clear_flag(fill_, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(fill_);

  knob_ = lv_obj_create(track_);
  lv_obj_set_size(knob_, knob_width_, inner_height);
  lv_obj_set_pos(knob_, inner_margin_, inner_margin_);
  gui2_core::set_surface_style(knob_, lv_color_hex(0x347FF1));
  lv_obj_set_style_radius(knob_, inner_height / 2, LV_PART_MAIN);
  lv_obj_add_flag(knob_, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(knob_);
  lv_obj_add_event_cb(knob_, event_callback, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(knob_, event_callback, LV_EVENT_PRESSING, this);
  lv_obj_add_event_cb(knob_, event_callback, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(knob_, event_callback, LV_EVENT_PRESS_LOST, this);
  lv_obj_t* arrow = create_svg_image(knob_, &kGui2IconSliderArrow, inner_height * 58 / 100,
                                     inner_height * 58 / 100);
  lv_obj_center(arrow);

  set_progress(0);
  return track_;
}

void swipe_slider::set_progress(int progress) {
  progress_ = std::clamp(progress, 0, 1000);
  if (track_ == nullptr || knob_ == nullptr || fill_ == nullptr) return;
  const int inner_width = lv_obj_get_width(track_) - inner_margin_ * 2;
  const int travel = std::max(0, inner_width - knob_width_);
  const int x = travel * progress_ / 1000;
  lv_obj_set_x(knob_, inner_margin_ + x);
  lv_obj_set_width(fill_, x + knob_width_);
  lv_obj_set_x(fill_, inner_margin_);
  lv_obj_set_width(prompt_, std::max(1, inner_width - knob_width_ + prompt_inset_));
  lv_obj_set_x(prompt_, inner_margin_ + knob_width_ - prompt_inset_);
}

void swipe_slider::update_from_point(lv_point_t point) {
  if (track_ == nullptr) return;
  lv_area_t area;
  lv_obj_get_coords(track_, &area);
  const int local_x = std::clamp(point.x - area.x1, 0, static_cast<int>(lv_area_get_width(&area)));
  const int inner_width = lv_obj_get_width(track_) - inner_margin_ * 2;
  const int travel = std::max(1, inner_width - knob_width_);
  set_progress((local_x - inner_margin_ - knob_width_ / 2) * 1000 / travel);
}

void swipe_slider::finish_drag(bool cancelled) {
  dragging_ = false;
  if (cancelled || progress_ < 1000) {
    set_progress(0);
    return;
  }
  if (callback_ != nullptr) callback_(user_data_);
}

void swipe_slider::event_callback(lv_event_t* event) {
  auto* slider = static_cast<swipe_slider*>(lv_event_get_user_data(event));
  if (slider == nullptr) return;
  lv_indev_t* indev = lv_event_get_indev(event);
  if (indev == nullptr) indev = lv_indev_active();
  if (indev == nullptr) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    slider->dragging_ = true;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    slider->update_from_point(point);
  } else if (code == LV_EVENT_PRESSING && slider->dragging_) {
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    slider->update_from_point(point);
  } else if (code == LV_EVENT_RELEASED) {
    slider->finish_drag(false);
  } else if (code == LV_EVENT_PRESS_LOST) {
    slider->finish_drag(true);
  }
}

void swipe_slider::reset() {
  dragging_ = false;
  set_progress(0);
}

void swipe_slider::detach() {
  track_ = nullptr;
  fill_ = nullptr;
  knob_ = nullptr;
  prompt_ = nullptr;
  callback_ = nullptr;
  user_data_ = nullptr;
  inner_margin_ = 0;
  knob_width_ = 0;
  prompt_inset_ = 0;
  progress_ = 0;
  dragging_ = false;
}

}  // namespace gui2_components
