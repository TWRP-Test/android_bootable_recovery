#include "gui2_input.h"

#include <linux/input.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <set>

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
static bool queued_screenshot;
static bool queued_toggle_screen;
static bool queued_back;
static bool queued_home;
static bool screen_off_input;
static bool touch_state_pending;
static lv_indev_state_t pending_touch_state = LV_INDEV_STATE_RELEASED;
static int primary_touch_id = -1;
static constexpr float kRelativeMouseScale = 2.5f;
static int power_key = KEY_POWER;

// HardwareKeyboard's state, and InputHandler's key repeat: 500 ms to the first
// repeat, then every 100 ms.
static std::set<int> pressed_keys;
static std::deque<int> typed_chars;
static std::deque<int> typed_keys;
static int last_key_char;
static int last_key;
static bool key_held;
static bool key_repeating;
static uint64_t key_timer_ms;
static constexpr int KEYBOARD_ACTION = 13;
static constexpr int KEYBOARD_BACKSPACE = 8;
static constexpr int KEYBOARD_TAB = 9;

static int clamp_pointer_x(int value) {
  return std::clamp(value, 0, std::max(0, gr_fb_width() - 1));
}

static int clamp_pointer_y(int value) {
  return std::clamp(value, 0, std::max(0, gr_fb_height() - 1));
}

static void update_relative_pointer(int delta_x, int delta_y) {
  if (delta_x != 0) {
    pointer_state.point.x = clamp_pointer_x(
        pointer_state.point.x + static_cast<int>(std::lround(delta_x * kRelativeMouseScale)));
  }
  if (delta_y != 0) {
    pointer_state.point.y = clamp_pointer_y(
        pointer_state.point.y + static_cast<int>(std::lround(delta_y * kRelativeMouseScale)));
  }
}

static uint64_t key_clock_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

static void queue_key_action(gui2_key_action action) {
  if (action == gui2_key_action::SCREENSHOT)
    queued_screenshot = true;
  else if (action == gui2_key_action::BACK)
    queued_back = true;
  else if (action == gui2_key_action::HOME)
    queued_home = true;
  else
    queued_toggle_screen = true;
}

// Map keys to other keys.
static int TranslateKeyCode(int key_code) {
  switch (key_code) {
    case KEY_SLEEP:  // Lock key on Asus Transformer hardware keyboard
      return KEY_POWER;
  }
  return key_code;
}

static int KeyCodeToChar(int key_code, bool shiftkey, bool ctrlkey) {
  int keyboard = -1;

  static const struct {
    int code;
    char plain;
    char shifted;
  } kKeys[] = {
    { KEY_A, 'a', 'A' }, { KEY_B, 'b', 'B' }, { KEY_C, 'c', 'C' }, { KEY_D, 'd', 'D' },
    { KEY_E, 'e', 'E' }, { KEY_F, 'f', 'F' }, { KEY_G, 'g', 'G' }, { KEY_H, 'h', 'H' },
    { KEY_I, 'i', 'I' }, { KEY_J, 'j', 'J' }, { KEY_K, 'k', 'K' }, { KEY_L, 'l', 'L' },
    { KEY_M, 'm', 'M' }, { KEY_N, 'n', 'N' }, { KEY_O, 'o', 'O' }, { KEY_P, 'p', 'P' },
    { KEY_Q, 'q', 'Q' }, { KEY_R, 'r', 'R' }, { KEY_S, 's', 'S' }, { KEY_T, 't', 'T' },
    { KEY_U, 'u', 'U' }, { KEY_V, 'v', 'V' }, { KEY_W, 'w', 'W' }, { KEY_X, 'x', 'X' },
    { KEY_Y, 'y', 'Y' }, { KEY_Z, 'z', 'Z' }, { KEY_0, '0', ')' }, { KEY_1, '1', '!' },
    { KEY_2, '2', '@' }, { KEY_3, '3', '#' }, { KEY_4, '4', '$' }, { KEY_5, '5', '%' },
    { KEY_6, '6', '^' }, { KEY_7, '7', '&' }, { KEY_8, '8', '*' }, { KEY_9, '9', '(' },
    { KEY_SLASH, '/', '?' }, { KEY_DOT, '.', '>' }, { KEY_COMMA, ',', '<' },
    { KEY_MINUS, '-', '_' }, { KEY_GRAVE, '`', '~' }, { KEY_EQUAL, '=', '+' },
    { KEY_LEFTBRACE, '[', '{' }, { KEY_RIGHTBRACE, ']', '}' }, { KEY_BACKSLASH, '\\', '|' },
    { KEY_SEMICOLON, ';', ':' }, { KEY_APOSTROPHE, '\'', '\"' },
  };
  for (const auto& entry : kKeys) {
    if (entry.code == key_code) {
      keyboard = shiftkey ? entry.shifted : entry.plain;
      break;
    }
  }
  switch (key_code) {
    case KEY_SPACE:
      keyboard = ' ';
      break;
    case KEY_BACKSPACE:
      keyboard = KEYBOARD_BACKSPACE;
      break;
    case KEY_TAB:
      keyboard = KEYBOARD_TAB;
      break;
    case KEY_ENTER:
      keyboard = KEYBOARD_ACTION;
      break;
  }
  if (ctrlkey) {
    if (keyboard >= 96)
      keyboard -= 96;
    else
      keyboard = -1;
  }
  return keyboard;
}

static bool is_key_down(int key_code) {
  return pressed_keys.find(key_code) != pressed_keys.end();
}

// HardwareKeyboard::KeyDown, with the page's side left to the loop.
static void keyboard_key_down(int key_code) {
  pressed_keys.insert(key_code);

  bool ctrlkey = is_key_down(KEY_LEFTCTRL) || is_key_down(KEY_RIGHTCTRL);
  bool shiftkey = is_key_down(KEY_LEFTSHIFT) || is_key_down(KEY_RIGHTSHIFT);

  int ch = KeyCodeToChar(key_code, shiftkey, ctrlkey);

  if (ch != -1) {
    last_key_char = ch;
    typed_chars.push_back(ch);
  } else {
    last_key_char = 0;
    // The page's <action key=...> runs once, on release (GUIAction::NotifyKey).
    if (key_code == KEY_BACK || key_code == KEY_HOMEPAGE) {
      last_key = 0;
    } else {
      last_key = key_code;
      typed_keys.push_back(key_code);
    }
  }
}

static void keyboard_key_up(int key_code) {
  if (pressed_keys.erase(key_code) == 0) return;
  if (key_code == KEY_BACK)
    queue_key_action(gui2_key_action::BACK);
  else if (key_code == KEY_HOMEPAGE)
    queue_key_action(gui2_key_action::HOME);
}

// HardwareKeyboard::KeyRepeat
static void keyboard_key_repeat(void) {
  if (last_key_char)
    typed_chars.push_back(last_key_char);
  else if (last_key)
    typed_keys.push_back(last_key);
}

static void process_key_event(const input_event& event) {
  if (event.type != EV_KEY) return;

  if (event.code == BTN_SIDE) {
    if (event.value == 1) queue_key_action(gui2_key_action::BACK);
    return;
  }

  const int code = TranslateKeyCode(event.code);
  if (code != power_key && code != KEY_VOLUMEDOWN) {
    if (event.value != 0) {
      // This is a key press
      keyboard_key_down(code);
      key_held = true;
      key_repeating = false;
      key_timer_ms = key_clock_ms();
    } else {
      // This is a key release
      keyboard_key_up(code);
      key_held = false;
    }
    return;
  }

  // Ignore key autorepeat.
  if (event.value == 2) return;

  if (code == power_key) {
    if (event.value != 0) {
      power_down = true;
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
  if (!power_down && !volume_down) key_combo_consumed = false;
}

// InputHandler::processHoldAndRepeat for keys.
static void repeat_held_key(void) {
  if (!key_held) return;
  const uint64_t now = key_clock_ms();
  if (!key_repeating && now - key_timer_ms > 500) {
    key_timer_ms = now;
    key_repeating = true;
    keyboard_key_repeat();
  } else if (key_repeating && now - key_timer_ms > 100) {
    key_timer_ms = now;
    keyboard_key_repeat();
  }
}

void gui2_input_set_power_key(int key_code) {
  power_key = key_code > 0 ? key_code : KEY_POWER;
}

bool gui2_input_take_char(int* ch) {
  repeat_held_key();
  if (ch == nullptr || typed_chars.empty()) return false;
  *ch = typed_chars.front();
  typed_chars.pop_front();
  return true;
}

bool gui2_input_take_key(int* key_code) {
  if (key_code == nullptr || typed_keys.empty()) return false;
  *key_code = typed_keys.front();
  typed_keys.pop_front();
  return true;
}

static void read_cb(lv_indev_t* indev __unused, lv_indev_data_t* data) {
  input_event event;
  TWRPTouchPoint touch_events[TWRP_MAX_TOUCH_EVENTS];
  lv_indev_touch_data_t gesture_touches[TWRP_MAX_TOUCH_EVENTS];
  bool touch_frame_seen = false;

  auto consume_touch_frame = [&]() {
    const int touch_count = twrp_input_take_touch_events(touch_events, TWRP_MAX_TOUCH_EVENTS);
    if (touch_count <= 0) return false;

    const int copied_touch_count = std::min(touch_count, TWRP_MAX_TOUCH_EVENTS);
    touch_frame_seen = true;
    input_activity = true;

    int primary_index = -1;
    if (primary_touch_id >= 0) {
      for (int i = 0; i < copied_touch_count; ++i) {
        if (touch_events[i].id == primary_touch_id && touch_events[i].pressed) {
          primary_index = i;
          break;
        }
      }
    }
    if (primary_index < 0) {
      for (int i = 0; i < copied_touch_count; ++i) {
        if (touch_events[i].pressed) {
          primary_index = i;
          break;
        }
      }
    }

    for (int i = 0; i < copied_touch_count; ++i) {
      gesture_touches[i].id = static_cast<uint8_t>(touch_events[i].id);
      gesture_touches[i].point.x = clamp_pointer_x(touch_events[i].x);
      gesture_touches[i].point.y = clamp_pointer_y(touch_events[i].y);
      gesture_touches[i].state =
          touch_events[i].pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      gesture_touches[i].timestamp = lv_tick_get();
    }

    if (primary_index >= 0) {
      primary_touch_id = touch_events[primary_index].id;
      pointer_state.point = gesture_touches[primary_index].point;
      pointer_state.state = LV_INDEV_STATE_PRESSED;
    } else {
      int release_index = -1;
      for (int i = 0; i < copied_touch_count; ++i) {
        if (touch_events[i].id == primary_touch_id) {
          release_index = i;
          break;
        }
      }
      if (release_index < 0) release_index = copied_touch_count - 1;
      pointer_state.point = gesture_touches[release_index].point;
      pointer_state.state = LV_INDEV_STATE_RELEASED;
      primary_touch_id = -1;
    }
    touch_state_pending = false;

#if LV_USE_GESTURE_RECOGNITION
    if (!screen_off_input) {
      lv_indev_gesture_recognizers_update(indev, gesture_touches,
                                          static_cast<uint16_t>(copied_touch_count));
    }
#endif
    return true;
  };

  if (consume_touch_frame()) {
    goto finish;
  }
  for (;;) {
    const int result = ev_get(&event, 0);
    if (consume_touch_frame()) break;
    if (result == -2) break;
    if (result != 0) continue;

    if (event.type == EV_KEY && event.code != BTN_LEFT && event.code != BTN_TOUCH) {
      process_key_event(event);
      if (TranslateKeyCode(event.code) != power_key) input_activity = true;
      break;
    }

    if (event.type == EV_ABS) {
      const int x = event.value >> 16;
      const int y = event.value & 0xffff;
      if (event.code == TWRP_ABS_MOUSE_POSITION) {
        pointer_state.point.x = clamp_pointer_x(x);
        pointer_state.point.y = clamp_pointer_y(y);
        input_activity = true;
        break;
      }
      if (event.code == 1 || event.code == 0) {
        pointer_state.point.x = clamp_pointer_x(x);
        pointer_state.point.y = clamp_pointer_y(y);
        if (touch_state_pending) {
          pointer_state.state = pending_touch_state;
          touch_state_pending = false;
        } else {
          pointer_state.state = event.code == 1 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        }
        input_activity = true;
      }
      break;
    }

    if (event.type == EV_KEY && (event.code == BTN_LEFT || event.code == BTN_TOUCH)) {
      input_activity = true;
      if (event.code == BTN_TOUCH) {
        pending_touch_state = event.value != 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        touch_state_pending = true;
        continue;
      }
      pointer_state.state = event.value != 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      break;
    }

    if (event.type == EV_REL && event.code == REL_X) {
      update_relative_pointer(event.value, 0);
      input_activity = true;
      break;
    }
    if (event.type == EV_REL && event.code == REL_Y) {
      update_relative_pointer(0, event.value);
      input_activity = true;
      break;
    }
    if (event.type == EV_REL && event.code == REL_WHEEL) {
      wheel_delta += event.value;
      input_activity = true;
      break;
    }
  }

finish:
#if LV_USE_GESTURE_RECOGNITION
  if (touch_frame_seen && !screen_off_input) lv_indev_gesture_recognizers_set_data(indev, data);
#endif

  data->point = pointer_state.point;
  data->state = screen_off_input ? LV_INDEV_STATE_RELEASED : pointer_state.state;
  data->timestamp = lv_tick_get();
  data->continue_reading = touch_frame_seen;
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
  if (queued_back) {
    queued_back = false;
    *action = gui2_key_action::BACK;
    return true;
  }
  if (queued_home) {
    queued_home = false;
    *action = gui2_key_action::HOME;
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
  pointer_state.point = { gr_fb_width() / 2, gr_fb_height() / 2 };
  wheel_delta = 0;
  input_activity = false;
  power_down = false;
  volume_down = false;
  key_combo_consumed = false;
  queued_screenshot = false;
  queued_toggle_screen = false;
  queued_back = false;
  queued_home = false;
  screen_off_input = false;
  primary_touch_id = -1;
  touch_state_pending = false;
  pending_touch_state = LV_INDEV_STATE_RELEASED;

  lv_indev_t* indev = lv_indev_create();
  if (!indev) return nullptr;

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return indev;
}
