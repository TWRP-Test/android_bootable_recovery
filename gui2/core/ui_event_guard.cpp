#include "core/ui_event_guard.h"

namespace gui2_core {

namespace {

lv_indev_t* pointer_indev;
gui2_backend::hardware_settings* hardware;
lv_obj_t* click_target;
bool click_cancelled;

}  // namespace

void configure_click_guard(lv_indev_t* new_pointer_indev,
                           gui2_backend::hardware_settings* new_hardware) {
  pointer_indev = new_pointer_indev;
  hardware = new_hardware;
  reset_click_guard();
}

void press_cancel_guard_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (code == LV_EVENT_PRESSED) {
    click_target = target;
    click_cancelled = false;
  } else if (code == LV_EVENT_PRESSING && click_target == target && pointer_indev != nullptr) {
    lv_point_t point;
    lv_indev_get_point(pointer_indev, &point);
    lv_area_t click_area;
    lv_obj_get_click_area(target, &click_area);
    if (point.x < click_area.x1 || point.x > click_area.x2 || point.y < click_area.y1 ||
        point.y > click_area.y2)
      click_cancelled = true;
  } else if (code == LV_EVENT_PRESS_LOST && click_target == target) {
    click_cancelled = true;
  }
}

void add_press_cancel_guard(lv_obj_t* object) {
  if (object == nullptr) return;
  lv_obj_add_event_cb(object, press_cancel_guard_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(object, press_cancel_guard_cb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(object, press_cancel_guard_cb, LV_EVENT_PRESS_LOST, nullptr);
}

bool accept_click(lv_event_t* event) {
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  const bool accepted = click_target == nullptr || (click_target == target && !click_cancelled);
  bool inside = true;
  if (accepted && pointer_indev != nullptr && target != nullptr) {
    lv_point_t point;
    lv_indev_get_point(pointer_indev, &point);
    lv_area_t click_area;
    lv_obj_get_click_area(target, &click_area);
    inside = point.x >= click_area.x1 && point.x <= click_area.x2 && point.y >= click_area.y1 &&
             point.y <= click_area.y2;
  }
  const bool result = accepted && inside;
  reset_click_guard();
  if (result && hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
  return result;
}

void clear_click_guard() {
  reset_click_guard();
}

void reset_click_guard() {
  click_target = nullptr;
  click_cancelled = false;
}

}  // namespace gui2_core
