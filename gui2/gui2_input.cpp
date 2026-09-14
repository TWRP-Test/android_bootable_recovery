#include "gui2_input.h"

#include <linux/input.h>
#include <algorithm>
#include <chrono>

#include "twrpminui/minui.h"

struct input_state {
  lv_point_t point = { 0, 0 };
  lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

static input_state pointer_state;
static int wheel_delta;
static bool input_activity;
static bool power_down;
static bool volume_down;
static bool key_combo_consumed;
static uint64_t power_down_ms;
static bool queued_screenshot;
static bool queued_toggle_screen;
static bool screen_off_input;

static uint64_t key_clock_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

static void queue_key_action(gui2_key_action action) {
  if (action == gui2_key_action::SCREENSHOT)
    queued_screenshot = true;
  else
    queued_toggle_screen = true;
}

static void process_key_event(const input_event& event) {
  if (event.type != EV_KEY || (event.code != KEY_POWER && event.code != KEY_VOLUMEDOWN)) return;

  // Ignore key autorepeat.
  if (event.value == 2) return;

  if (event.code == KEY_POWER) {
    if (event.value != 0) {
      if (!power_down) {
        power_down = true;
        power_down_ms = key_clock_ms();
      }
      if (volume_down && !key_combo_consumed) {
        key_combo_consumed = true;
        queued_screenshot = true;
      }
    } else {
      const bool was_down = power_down;
      power_down = false;
      // Queue a short press unless it formed a screenshot chord.
      if (was_down && !key_combo_consumed) {
        key_combo_consumed = true;
        queue_key_action(gui2_key_action::TOGGLE_SCREEN);
      }
    }
    return;
  }

  if (event.value != 0) {
    volume_down = true;
    if (power_down && !key_combo_consumed) {
      key_combo_consumed = true;
      queued_screenshot = true;
    }
  } else {
    volume_down = false;
    if (!power_down) key_combo_consumed = false;
  }
}

static void expire_power_key(void) {
  // Wait briefly for the screenshot chord partner.
  if (power_down && !key_combo_consumed && key_clock_ms() - power_down_ms >= 250) {
    key_combo_consumed = true;
    queue_key_action(gui2_key_action::TOGGLE_SCREEN);
  }

  if (!power_down && !volume_down) key_combo_consumed = false;
}

static void read_cb(lv_indev_t* indev __unused, lv_indev_data_t* data) {
  input_event event;
  TWRPTouchPoint touch_events[TWRP_MAX_TOUCH_EVENTS];
  lv_indev_touch_data_t gesture_touches[TWRP_MAX_TOUCH_EVENTS];
  bool touch_frame_seen = false;
  lv_point_t latest_touch_point = pointer_state.point;
  lv_indev_state_t latest_touch_state = LV_INDEV_STATE_RELEASED;

  for (;;) {
    const int result = ev_get(&event, 0);

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
        for (int i = 0; i < copied_touch_count; ++i) {
          if (gesture_touches[i].state == LV_INDEV_STATE_PRESSED) {
            latest_touch_point = gesture_touches[i].point;
            latest_touch_state = LV_INDEV_STATE_PRESSED;
            break;
          }
        }
      }

#if LV_USE_GESTURE_RECOGNITION
      if (!screen_off_input) {
        lv_indev_gesture_recognizers_update(indev, gesture_touches,
                                            static_cast<uint16_t>(copied_touch_count));
      }
#endif
    }

    if (result == -2) break;
    if (result != 0) continue;

    if (event.type == EV_KEY && event.code != BTN_LEFT && event.code != BTN_TOUCH) {
      process_key_event(event);
      if (event.code != KEY_POWER && event.code != KEY_VOLUMEDOWN) input_activity = true;
      continue;
    }

    if (event.type == EV_ABS) {
      const int x = event.value >> 16;
      const int y = event.value & 0xffff;

      if (event.code == TWRP_ABS_MOUSE_POSITION) {
        pointer_state.point.x = std::clamp(x, 0, std::max(0, gr_fb_width() - 1));
        pointer_state.point.y = std::clamp(y, 0, std::max(0, gr_fb_height() - 1));
        input_activity = true;
        continue;
      }

      if (event.code == 1 || event.code == 0) {
        pointer_state.point.x = std::clamp(x, 0, std::max(0, gr_fb_width() - 1));
        pointer_state.point.y = std::clamp(y, 0, std::max(0, gr_fb_height() - 1));
        pointer_state.state = event.code == 1 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        input_activity = true;
      }
      continue;
    }

    if (event.type == EV_KEY && event.code == BTN_LEFT) {
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
      wheel_delta += event.value;
      input_activity = true;
    }
  }

#if LV_USE_GESTURE_RECOGNITION
  if (touch_frame_seen && !screen_off_input) lv_indev_gesture_recognizers_set_data(indev, data);
#endif

  if (touch_frame_seen) {
    pointer_state.point = latest_touch_point;
    pointer_state.state = latest_touch_state;
  }

  data->point = pointer_state.point;
  data->state = screen_off_input ? LV_INDEV_STATE_RELEASED : pointer_state.state;
}

bool gui2_input_take_activity(void) {
  const bool activity = input_activity;
  input_activity = false;
  return activity;
}

void gui2_input_set_screen_off(bool screen_off) {
  screen_off_input = screen_off;
}

bool gui2_input_take_key_action(gui2_key_action* action) {
  if (action == nullptr) return false;
  expire_power_key();
  if (queued_screenshot) {
    queued_screenshot = false;
    *action = gui2_key_action::SCREENSHOT;
    return true;
  }
  if (queued_toggle_screen) {
    queued_toggle_screen = false;
    *action = gui2_key_action::TOGGLE_SCREEN;
    return true;
  }
  return false;
}

int gui2_input_take_wheel(void) {
  const int delta = wheel_delta;
  wheel_delta = 0;
  return delta;
}

lv_indev_t* gui2_input_init(void) {
  pointer_state = {};
  wheel_delta = 0;
  input_activity = false;
  power_down = false;
  volume_down = false;
  key_combo_consumed = false;
  power_down_ms = 0;
  queued_screenshot = false;
  queued_toggle_screen = false;
  screen_off_input = false;

  lv_indev_t* indev = lv_indev_create();
  if (!indev) return nullptr;

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return indev;
}
