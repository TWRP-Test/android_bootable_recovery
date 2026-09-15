#include "shell/quick_panel_controller.h"

#include <time.h>
#include <algorithm>
#include <cmath>

namespace gui2_shell {

void quick_panel_controller::initialize(const gui2_core::ui_metrics& metrics,
                                        const quick_panel_view& view, lv_indev_t* pointer_indev,
                                        void (*on_open)()) {
  metrics_ = metrics;
  view_ = view;
  pointer_indev_ = pointer_indev;
  on_open_ = on_open;
  progress_ = 0;
  animation_target_open_ = false;
  reset_drag();
  set_progress(0);
}

bool quick_panel_controller::input_active() const {
  return view_.dismiss != nullptr &&
         (!lv_obj_has_flag(view_.dismiss, LV_OBJ_FLAG_HIDDEN) || gesture_tracking_);
}

uint64_t quick_panel_controller::monotonic_ms() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

void quick_panel_controller::set_progress(int progress) {
  progress_ = std::clamp(progress, 0, 1000);
  if (view_.menu != nullptr) {
    const int y =
        view_.menu_closed_y + (view_.menu_open_y - view_.menu_closed_y) * progress_ / 1000;
    lv_obj_set_y(view_.menu, y);
    lv_obj_set_style_bg_opa(view_.menu, LV_OPA_COVER, LV_PART_MAIN);
  }
  if (view_.dismiss != nullptr)
    lv_obj_set_style_bg_opa(view_.dismiss, static_cast<lv_opa_t>(progress_ * 30 / 1000),
                            LV_PART_MAIN);
}

void quick_panel_controller::animation_exec(void* object, int32_t progress) {
  auto* controller = static_cast<quick_panel_controller*>(object);
  if (controller != nullptr) controller->set_progress(progress);
}

void quick_panel_controller::animation_ready(lv_anim_t* animation) {
  if (animation == nullptr) return;
  auto* controller = static_cast<quick_panel_controller*>(lv_anim_get_user_data(animation));
  if (controller == nullptr || controller->animation_target_open_) return;
  if (controller->view_.menu != nullptr)
    lv_obj_add_flag(controller->view_.menu, LV_OBJ_FLAG_HIDDEN);
  if (controller->view_.dismiss != nullptr)
    lv_obj_add_flag(controller->view_.dismiss, LV_OBJ_FLAG_HIDDEN);
}

void quick_panel_controller::animate(bool open) {
  if (view_.menu == nullptr || view_.dismiss == nullptr) return;
  animation_target_open_ = open;
  lv_anim_del(this, animation_exec);
  if (open) {
    if (on_open_ != nullptr) on_open_();
    lv_obj_clear_flag(view_.dismiss, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view_.menu, LV_OBJ_FLAG_HIDDEN);
  }

  const int distance = std::abs((open ? 1000 : 0) - progress_);
  if (distance == 0) {
    if (!open) {
      lv_obj_add_flag(view_.menu, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(view_.dismiss, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, progress_, open ? 1000 : 0);
  lv_anim_set_duration(&animation, std::clamp(120 + distance / 10, 120, 220));
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, animation_exec);
  lv_anim_set_ready_cb(&animation, animation_ready);
  lv_anim_start(&animation);
}

void quick_panel_controller::open() {
  animate(true);
}

void quick_panel_controller::close() {
  if (view_.menu != nullptr) lv_anim_del(this, animation_exec);
  animation_target_open_ = false;
  set_progress(0);
  if (view_.menu != nullptr) lv_obj_add_flag(view_.menu, LV_OBJ_FLAG_HIDDEN);
  if (view_.dismiss != nullptr) lv_obj_add_flag(view_.dismiss, LV_OBJ_FLAG_HIDDEN);
  reset_drag();
}

lv_indev_t* quick_panel_controller::event_indev(lv_event_t* event) const {
  lv_indev_t* indev = event == nullptr ? nullptr : lv_event_get_indev(event);
  return indev == nullptr ? pointer_indev_ : indev;
}

void quick_panel_controller::begin_drag(lv_event_t* event, bool from_dismiss) {
  lv_indev_t* indev = event_indev(event);
  if (indev == nullptr || view_.menu == nullptr || view_.dismiss == nullptr) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_anim_del(this, animation_exec);
  lv_obj_clear_flag(view_.dismiss, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(view_.menu, LV_OBJ_FLAG_HIDDEN);
  gesture_start_y_ = point.y;
  gesture_last_y_ = point.y;
  gesture_last_ms_ = monotonic_ms();
  gesture_velocity_y_ = 0;
  gesture_start_progress_ = progress_;
  gesture_moved_ = false;
  gesture_from_dismiss_ = from_dismiss;
  gesture_tracking_ = true;
}

void quick_panel_controller::update_drag(lv_event_t* event) {
  if (!gesture_tracking_) return;
  lv_indev_t* indev = event_indev(event);
  if (indev == nullptr) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  const uint64_t now_ms = monotonic_ms();
  const int delta = point.y - gesture_start_y_;
  if (std::abs(delta) >= gui2_core::ui_px(12)) gesture_moved_ = true;
  const uint64_t elapsed_ms = now_ms - gesture_last_ms_;
  if (elapsed_ms > 0)
    gesture_velocity_y_ = static_cast<int>((point.y - gesture_last_y_) * 1000 / elapsed_ms);
  gesture_last_y_ = point.y;
  gesture_last_ms_ = now_ms;
  const int travel = std::max(1, view_.menu_open_y - view_.menu_closed_y);
  set_progress(gesture_start_progress_ + delta * 1000 / travel);
}

void quick_panel_controller::finish_drag() {
  gesture_tracking_ = false;
  const int touch_slop = gui2_core::ui_px(12);
  const int fling_velocity = gui2_core::ui_px(500);
  bool open = progress_ >= 500;
  if (gesture_from_dismiss_) {
    const bool upward = gesture_start_y_ - gesture_last_y_ >= touch_slop;
    const bool upward_fling = gesture_velocity_y_ <= -fling_velocity;
    open = !(gesture_moved_ && (upward || upward_fling));
  } else if (gesture_velocity_y_ >= fling_velocity) {
    open = true;
  } else if (gesture_velocity_y_ <= -fling_velocity) {
    open = false;
  }
  gesture_from_dismiss_ = false;
  animate(open);
}

void quick_panel_controller::reset_drag() {
  gesture_tracking_ = false;
  gesture_moved_ = false;
  gesture_from_dismiss_ = false;
  gesture_velocity_y_ = 0;
  gesture_last_ms_ = 0;
}

}  // namespace gui2_shell
