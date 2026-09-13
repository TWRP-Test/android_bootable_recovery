#include "gui2_input.h"

#include <linux/input.h>
#include <algorithm>

#include "twrpminui/minui.h"

struct input_state {
  lv_point_t point = { 0, 0 };
  lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

static input_state pointer_state;
static int wheel_delta;
static bool input_activity;

static void read_cb(lv_indev_t* indev __unused, lv_indev_data_t* data) {
  input_event event;
  TWRPTouchPoint touch_events[TWRP_MAX_TOUCH_EVENTS];
  lv_indev_touch_data_t gesture_touches[TWRP_MAX_TOUCH_EVENTS];
  bool touch_frame_seen = false;
  lv_point_t latest_touch_point = pointer_state.point;
  lv_indev_state_t latest_touch_state = LV_INDEV_STATE_RELEASED;

  // ev_get() returns -1 when it consumed a low-level event while assembling
  // a TWRP touch report (ABS/SYN), not only when the queue is empty. Keep
  // polling in that case or one complete touch report can take several
  // LVGL input timer periods to reach the pointer driver.
  for (;;) {
    const int result = ev_get(&event, 0);

    // ev_get() may consume ABS/SYN events without returning a legacy event,
    // so collect a completed multitouch frame independently of result.
    const int touch_count = twrp_input_take_touch_events(touch_events, TWRP_MAX_TOUCH_EVENTS);
    if (touch_count >= 0) {
      const int copied_touch_count = std::min(touch_count, TWRP_MAX_TOUCH_EVENTS);
      touch_frame_seen = true;
      input_activity = true;

      int primary_index = -1;
      for (int i = 0; i < copied_touch_count; ++i) {
        gesture_touches[i].id = static_cast<uint8_t>(touch_events[i].id);
        gesture_touches[i].point.x =
            std::clamp(touch_events[i].x, 0, std::max(0, gr_fb_width() - 1));
        gesture_touches[i].point.y =
            std::clamp(touch_events[i].y, 0, std::max(0, gr_fb_height() - 1));
        gesture_touches[i].state =
            touch_events[i].pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        gesture_touches[i].timestamp = lv_tick_get();

        if (primary_index < 0 || touch_events[i].pressed) primary_index = i;
      }

      if (primary_index >= 0) {
        latest_touch_point = gesture_touches[primary_index].point;
        latest_touch_state = gesture_touches[primary_index].state;
        // Prefer an active contact when a frame contains both moves
        // and releases. This keeps the normal pointer path pressed
        // while the gesture recognizer receives the release event.
        for (int i = 0; i < copied_touch_count; ++i) {
          if (gesture_touches[i].state == LV_INDEV_STATE_PRESSED) {
            latest_touch_point = gesture_touches[i].point;
            latest_touch_state = LV_INDEV_STATE_PRESSED;
            break;
          }
        }
      }

#if LV_USE_GESTURE_RECOGNITION
      lv_indev_gesture_recognizers_update(indev, gesture_touches,
                                          static_cast<uint16_t>(copied_touch_count));
#endif
    }

    if (result == -2) break;
    if (result != 0) continue;

    if (event.type == EV_ABS) {
      const int x = event.value >> 16;
      const int y = event.value & 0xffff;

      // ev_get() emits this synthetic event for absolute pointing
      // devices such as QEMU's usb-tablet. It updates the position only;
      // the button state arrives separately as EV_KEY/BTN_LEFT.
      if (event.code == TWRP_ABS_MOUSE_POSITION) {
        pointer_state.point.x = std::clamp(x, 0, std::max(0, gr_fb_width() - 1));
        pointer_state.point.y = std::clamp(y, 0, std::max(0, gr_fb_height() - 1));
        input_activity = true;
        continue;
      }

      // Preserve the legacy TWRP touch event format, where EV_ABS code
      // 1 means press and code 0 means release.
      if (event.code == 1 || event.code == 0) {
        pointer_state.point.x = std::clamp(x, 0, std::max(0, gr_fb_width() - 1));
        pointer_state.point.y = std::clamp(y, 0, std::max(0, gr_fb_height() - 1));
        pointer_state.state = event.code == 1 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        input_activity = true;
      }
      continue;
    }

    if (event.type == EV_KEY && event.code == BTN_LEFT) {
      // QEMU's usb-tablet reports the mouse button as EV_KEY.
      pointer_state.state = event.value != 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      input_activity = true;
      continue;
    }

    if (event.type == EV_KEY && event.code == BTN_TOUCH) {
      pointer_state.state = event.value != 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      input_activity = true;
      continue;
    }

    if (event.type == EV_REL && event.code == REL_WHEEL) {
      // TWRP's event layer preserves Linux wheel events. Keep the
      // steps for the demo UI to apply to its scrollable list.
      wheel_delta += event.value;
      input_activity = true;
    }
  }

#if LV_USE_GESTURE_RECOGNITION
  if (touch_frame_seen) lv_indev_gesture_recognizers_set_data(indev, data);
#endif

  // The legacy event path may emit a release when one slot goes away even
  // though another slot is still active. Restore the true primary touch
  // state after consuming all low-level events in this read cycle.
  if (touch_frame_seen) {
    pointer_state.point = latest_touch_point;
    pointer_state.state = latest_touch_state;
  }

  data->point = pointer_state.point;
  data->state = pointer_state.state;
}

bool gui2_input_take_activity(void) {
  const bool activity = input_activity;
  input_activity = false;
  return activity;
}

int gui2_input_take_wheel(void) {
  const int delta = wheel_delta;
  wheel_delta = 0;
  return delta;
}

lv_indev_t* gui2_input_init(void) {
  lv_indev_t* indev = lv_indev_create();
  if (!indev) return nullptr;

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return indev;
}
