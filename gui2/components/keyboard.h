#ifndef GUI2_COMPONENTS_KEYBOARD_H
#define GUI2_COMPONENTS_KEYBOARD_H

#include <string>
#include <vector>

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_components {

// On-screen keyboard built from ordinary buttons rather than LVGL's button
// matrix. The matrix cannot colour one key differently from the rest, and its
// handler treats several glyphs as its own, which left the close key doing
// nothing until it was intercepted. Owning the keys outright gets the layout
// the design asks for and keeps the behaviour in one place.

enum class keyboard_layout {
  LETTERS,
  SYMBOLS,
  NUMBER,
};

using keyboard_callback = void (*)(void* user_data);

struct keyboard_options {
  lv_obj_t* parent = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  lv_obj_t* textarea = nullptr;
  keyboard_layout layout = keyboard_layout::LETTERS;
  // Pages that type into a field the user has to tap first start hidden.
  bool start_hidden = false;
  // An extra row of the punctuation a shell needs, which is otherwise two
  // layout switches away.
  bool shell_row = false;
  // The enter key. Without one it simply puts the keyboard away, and the hide
  // key always does.
  keyboard_callback accept_callback = nullptr;
  keyboard_callback hidden_callback = nullptr;
  keyboard_callback shown_callback = nullptr;
  // Fired for every key so the caller can add its own feedback.
  keyboard_callback key_callback = nullptr;
  void* user_data = nullptr;
};

// Blank space kept under the bottom row, so the keys clear the gesture bar by
// as much as the user wants. Held here rather than passed in, because every
// page that reserves keyboard space has to agree on it.
void set_keyboard_lift(int pixels);
int keyboard_lift();

// The height the keyboard occupies, lift included, so a page can reserve the
// space before the keyboard itself exists.
int keyboard_height(const gui2_core::ui_metrics& metrics, keyboard_layout layout,
                    bool shell_row = false);

// The same without the user's bottom margin. A page that reserves space for a
// keyboard should use this: the margin is empty panel, and letting it eat the
// page's own content is what makes the output box shrink as the slider moves.
int keyboard_base_height(const gui2_core::ui_metrics& metrics, keyboard_layout layout,
                         bool shell_row = false);

class keyboard {
 public:
  lv_obj_t* create(const keyboard_options& options);
  // Forgets the objects without deleting them, for when the page that owns
  // them is torn down.
  void detach();

  void show();
  void hide();
  // Deletes the objects. detach() only forgets them, which leaves a keyboard
  // parented to the top layer on screen after its page is gone.
  void dismiss();
  bool visible() const;

  // Types into this field, and shows the keyboard, when the field is tapped.
  // One keyboard can serve several fields this way.
  void bind(lv_obj_t* textarea);

  lv_obj_t* root() const {
    return root_;
  }

 private:
  enum class key_kind {
    CHARACTER,
    SHIFT,
    BACKSPACE,
    ENTER,
    TO_SYMBOLS,
    TO_LETTERS,
    HIDE,
    CURSOR_LEFT,
    CURSOR_RIGHT,
    COPY,
    PASTE,
    CARET,  // the inert divider inside the cursor group
  };

  struct key {
    key_kind kind = key_kind::CHARACTER;
    std::string label;   // what the key shows
    std::string text;    // what it types, when it types
    float weight = 1.0f;
    bool symbol_font = false;
    bool accent = false;
    bool subdued = false;  // the function row and the modifier keys
    // Adjacent keys sharing a non-zero id are drawn on one background, the way
    // the cursor pad reads as a single control rather than three keys.
    int group = 0;
  };

  void build_rows();
  void clear_rows();
  lv_obj_t* add_row(int height, int gap);
  lv_obj_t* add_key(lv_obj_t* parent, const key& definition, int width, int height, int radius,
                    bool inside_group);
  void handle(const key& definition);
  static void key_event_cb(lv_event_t* event);
  static void focus_event_cb(lv_event_t* event);

  std::vector<key> function_row() const;
  std::vector<key> shell_row() const;
  std::vector<std::vector<key>> letter_rows() const;
  std::vector<std::vector<key>> symbol_rows() const;
  std::vector<std::vector<key>> number_rows() const;

  lv_obj_t* root_ = nullptr;
  lv_obj_t* rows_ = nullptr;
  const gui2_core::ui_metrics* metrics_ = nullptr;
  const gui2_i18n::language_pack* strings_ = nullptr;
  lv_obj_t* textarea_ = nullptr;
  keyboard_layout layout_ = keyboard_layout::LETTERS;
  bool shifted_ = true;
  bool shell_row_ = false;
  keyboard_callback accept_callback_ = nullptr;
  keyboard_callback hidden_callback_ = nullptr;
  keyboard_callback shown_callback_ = nullptr;
  keyboard_callback key_callback_ = nullptr;
  void* user_data_ = nullptr;
  static void slide_y_cb(void* target, int32_t value);
  static void hidden_anim_done(lv_anim_t* anim);
  int resting_y_ = 0;

  std::vector<key> keys_;  // stable storage the buttons point into
};

}  // namespace gui2_components

#endif  // GUI2_COMPONENTS_KEYBOARD_H
