#include "components/keyboard_maps.h"

#include <stdint.h>
#include <string.h>

namespace gui2_components {

namespace {

// LVGL's default handler recognises the mode keys by their text, so these have
// to stay spelled exactly as it spells them.
constexpr const char* kToSpecial = "1#";
constexpr const char* kToUpper = "ABC";
constexpr const char* kToLower = "abc";

// The key that puts the keyboard away. LVGL only knows its own close symbols,
// so this one is handled below.
constexpr const char* kHide = LV_SYMBOL_DOWN;

constexpr uint32_t kControl = LV_KEYBOARD_CTRL_BUTTON_FLAGS;
constexpr uint32_t kChecked = LV_BUTTONMATRIX_CTRL_CHECKED;

// A width or'd with flags is an int, and C++ will not put one back into the
// enum on its own the way the C sources LVGL ships can.
constexpr lv_buttonmatrix_ctrl_t ctrl(uint32_t width, uint32_t flags = 0) {
  return static_cast<lv_buttonmatrix_ctrl_t>(flags | width);
}

const char* const kMapLower[] = {
    kToSpecial, "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    kToUpper, "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    kHide, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, kHide, ""};

const char* const kMapUpper[] = {
    kToSpecial, "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    kToLower, "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    kHide, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, kHide, ""};

// 12 + 11 + 12 + 6 keys, and one control entry per key.
const lv_buttonmatrix_ctrl_t kCtrlText[] = {
    ctrl(5, kControl), ctrl(4), ctrl(4), ctrl(4), ctrl(4), ctrl(4), ctrl(4), ctrl(4), ctrl(4),
    ctrl(4), ctrl(4), ctrl(7, kChecked),
    ctrl(6, kControl), ctrl(3), ctrl(3), ctrl(3), ctrl(3), ctrl(3), ctrl(3), ctrl(3), ctrl(3),
    ctrl(3), ctrl(7, kChecked),
    ctrl(1, kChecked), ctrl(1, kChecked), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1),
    ctrl(1), ctrl(1, kChecked), ctrl(1, kChecked), ctrl(1, kChecked),
    ctrl(2, kControl), ctrl(2, kChecked), ctrl(6), ctrl(2, kChecked), ctrl(2, kControl),
    ctrl(2, kControl)};

const char* const kMapSpecial[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    kToLower, "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    kHide, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, kHide, ""};

// 11 + 12 + 12 + 6 keys.
const lv_buttonmatrix_ctrl_t kCtrlSpecial[] = {
    ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1),
    ctrl(2, kChecked),
    ctrl(2, kControl), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1),
    ctrl(1), ctrl(1), ctrl(1),
    ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1), ctrl(1),
    ctrl(1), ctrl(1),
    ctrl(2, kControl), ctrl(2, kChecked), ctrl(6), ctrl(2, kChecked), ctrl(2, kControl),
    ctrl(2, kControl)};

// A PIN needs digits and nothing else, so the stock "+/-" and "." keys make way
// for the hide key in the bottom corner.
const char* const kMapNumber[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_LEFT, "\n",
    "7", "8", "9", LV_SYMBOL_RIGHT, "\n",
    kHide, "0", LV_SYMBOL_OK, ""};

// 4 + 4 + 4 + 3 keys. Four equal columns, so the short bottom row needs one key
// spanning two of them; the confirm key takes the pair and the rest stay a
// single column wide.
const lv_buttonmatrix_ctrl_t kCtrlNumber[] = {
    ctrl(1), ctrl(1), ctrl(1), ctrl(1, kChecked),
    ctrl(1), ctrl(1), ctrl(1), ctrl(1, kChecked),
    ctrl(1), ctrl(1), ctrl(1), ctrl(1, kChecked),
    ctrl(1, kControl), ctrl(1), ctrl(2, kControl)};

// LVGL only acts on its own close symbols and types anything else into the text
// area, so the hide key is turned into a close request here and everything else
// is left to the stock handler.
void keyboard_event_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (keyboard == nullptr) return;
  const uint32_t button = lv_keyboard_get_selected_button(keyboard);
  const char* text = lv_keyboard_get_button_text(keyboard, button);
  if (text != nullptr && strcmp(text, kHide) == 0) {
    lv_obj_send_event(keyboard, LV_EVENT_CANCEL, nullptr);
    return;
  }
  lv_keyboard_def_event_cb(event);
}

}  // namespace

void install_keyboard_maps(lv_obj_t* keyboard) {
  if (keyboard == nullptr) return;
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kMapLower, kCtrlText);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kMapUpper, kCtrlText);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, kMapSpecial, kCtrlSpecial);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_NUMBER, kMapNumber, kCtrlNumber);
  lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

}  // namespace gui2_components
