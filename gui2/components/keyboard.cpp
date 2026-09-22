#include "components/keyboard.h"

#include <algorithm>
#include <string.h>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

constexpr uint32_t kPanel = 0x141414;

// Set from settings before any keyboard exists.
int lift_pixels = 0;
constexpr uint32_t kKey = 0x3C3C3C;
constexpr uint32_t kKeyPressed = 0x585858;
constexpr uint32_t kKeySubdued = 0x2E2E2E;
constexpr uint32_t kAccent = 0x347FF1;

// A clipboard of its own: recovery has no system one, and the copy and paste
// keys are useless without somewhere to put the text.
std::string& clipboard() {
  static std::string buffer;
  return buffer;
}

struct geometry {
  int side_pad;
  int gap;
  int key_height;
  int function_height;
  int rows;
};

geometry measure(const gui2_core::ui_metrics& metrics, keyboard_layout layout,
                 bool shell_row = false) {
  geometry g;
  g.key_height = std::max(1, metrics.height * 11 / 200);
  g.function_height = g.key_height * 4 / 5;
  g.gap = std::max(2, g.key_height / 11);
  g.side_pad = g.gap * 2;
  g.rows = shell_row ? 5 : 4;
  return g;
}

// LVGL leaves the cursor invisible unless the style asks for it, which makes
// the arrow keys look broken, and without a selection there is nothing for the
// copy key to take. Both are properties of the field, so set them once per
// field rather than per keypress.
void prepare_field(lv_obj_t* textarea, lv_color_t caret) {
  if (textarea == nullptr) return;
  // One caret, not two. LVGL fills LV_PART_CURSOR as a block and redraws the
  // letter on top of it, which reads as a coloured box behind a white bar.
  // A left border alone gives the single thin caret people expect.
  lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
  lv_obj_set_style_border_width(textarea, gui2_core::ui_px(5), LV_PART_CURSOR);
  lv_obj_set_style_border_color(textarea, caret, LV_PART_CURSOR);
  lv_obj_set_style_border_opa(textarea, LV_OPA_COVER, LV_PART_CURSOR);
  lv_obj_set_style_text_color(textarea, caret, LV_PART_CURSOR);
  lv_obj_set_style_bg_color(textarea, lv_color_hex(kAccent), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_50, LV_PART_SELECTED);
  lv_textarea_set_text_selection(textarea, true);
  // Without this the page underneath takes the drag and scrolls instead of
  // letting the field select.
  lv_obj_remove_flag(textarea, LV_OBJ_FLAG_SCROLL_CHAIN);
}

// LVGL blinks the cursor from an animation it only starts when the field takes
// focus from an input group. These pages have no group, so the animation never
// runs and the cursor sits lit. Drive it here instead: one timer, one field,
// and the state is ours to reason about.
constexpr uint32_t kBlinkMs = 500;
lv_timer_t* blink_timer = nullptr;
lv_obj_t* blink_field = nullptr;
bool blink_on = true;

void blink_timer_cb(lv_timer_t*) {
  if (blink_field == nullptr) return;
  blink_on = !blink_on;
  lv_obj_set_style_border_opa(blink_field, blink_on ? LV_OPA_COVER : LV_OPA_TRANSP,
                              LV_PART_CURSOR);
}

void blink_field_is(lv_obj_t* textarea) {
  if (blink_field != nullptr && blink_field != textarea)
    lv_obj_set_style_border_opa(blink_field, LV_OPA_COVER, LV_PART_CURSOR);
  blink_field = textarea;
  blink_on = true;
  if (textarea == nullptr) return;
  lv_obj_set_style_border_opa(textarea, LV_OPA_COVER, LV_PART_CURSOR);
  if (blink_timer == nullptr) blink_timer = lv_timer_create(blink_timer_cb, kBlinkMs, nullptr);
  lv_timer_reset(blink_timer);
}

void blink_stop(void) {
  if (blink_timer != nullptr) {
    lv_timer_delete(blink_timer);
    blink_timer = nullptr;
  }
  blink_field = nullptr;
}

// The selected range, as byte offsets into the field's text. Returns false when
// nothing is selected.
bool selected_range(lv_obj_t* textarea, const char* text, size_t* from, size_t* to) {
  lv_obj_t* label = lv_textarea_get_label(textarea);
  if (label == nullptr || text == nullptr) return false;
  const uint32_t start = lv_label_get_text_selection_start(label);
  const uint32_t end = lv_label_get_text_selection_end(label);
  if (start == LV_LABEL_TEXT_SELECTION_OFF || end == LV_LABEL_TEXT_SELECTION_OFF || end <= start)
    return false;

  // Selection indices count characters, not bytes; walk the UTF-8 to convert.
  size_t byte = 0;
  uint32_t character = 0;
  size_t begin = std::string::npos;
  while (text[byte] != '\0') {
    if (character == start) begin = byte;
    if (character == end) break;
    const unsigned char c = static_cast<unsigned char>(text[byte]);
    byte += c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
    ++character;
  }
  if (begin == std::string::npos) return false;
  *from = begin;
  *to = byte;
  return *to > *from;
}

}  // namespace

int keyboard_lift() {
  return lift_pixels;
}

void set_keyboard_lift(int pixels) {
  lift_pixels = std::max(0, pixels);
}

int keyboard_base_height(const gui2_core::ui_metrics& metrics, keyboard_layout layout,
                         bool shell_row) {
  const geometry g = measure(metrics, layout, shell_row);
  return g.side_pad * 2 + g.function_height + g.rows * g.key_height + g.rows * g.gap;
}

int keyboard_height(const gui2_core::ui_metrics& metrics, keyboard_layout layout,
                    bool shell_row) {
  return keyboard_base_height(metrics, layout, shell_row) + keyboard_lift();
}

// The characters a shell needs constantly. Without these every pipe or
// redirect costs two layout switches.
std::vector<keyboard::key> keyboard::shell_row() const {
  std::vector<key> row;
  for (const char* glyph : { "-", "_", "/", "|", "~", "*", "&", ">", "<", "$" })
    row.push_back({ key_kind::CHARACTER, glyph, glyph, 1.0f, false, false, true, 0 });
  return row;
}

std::vector<keyboard::key> keyboard::function_row() const {
  const char* copy = strings_ != nullptr ? strings_->keyboard_copy : "Copy";
  const char* paste = strings_ != nullptr ? strings_->keyboard_paste : "Paste";

  std::vector<key> row;
  // The cursor pad is one control: the caret in the middle only marks where the
  // arrows act, so it shares their background and does nothing when pressed.
  row.push_back({key_kind::CURSOR_LEFT, LV_SYMBOL_LEFT, "", 1.0f, true, false, true, 1});
  row.push_back({key_kind::CARET, "I", "", 0.7f, false, false, true, 1});
  row.push_back({key_kind::CURSOR_RIGHT, LV_SYMBOL_RIGHT, "", 1.0f, true, false, true, 1});
  row.push_back({key_kind::COPY, copy, "", 1.6f, false, false, true, 0});
  row.push_back({key_kind::PASTE, paste, "", 1.6f, false, false, true, 0});
  // A PIN has no use for punctuation, and dropping it lets the keys that are
  // left take the width instead.
  if (layout_ != keyboard_layout::NUMBER) {
    row.push_back({key_kind::CHARACTER, "/", "/", 1.0f, false, false, true, 0});
    row.push_back({key_kind::CHARACTER, "\"", "\"", 1.0f, false, false, true, 0});
    row.push_back({key_kind::CHARACTER, "'", "'", 1.0f, false, false, true, 0});
  }
  row.push_back({key_kind::HIDE, LV_SYMBOL_KEYBOARD "\n" LV_SYMBOL_DOWN, "", 1.3f, true, false,
                 true, 0});
  return row;
}

std::vector<std::vector<keyboard::key>> keyboard::letter_rows() const {
  static const char* kTop = "qwertyuiop";
  static const char* kMiddle = "asdfghjkl";
  static const char* kBottom = "zxcvbnm";

  auto letters = [this](const char* source) {
    std::vector<key> row;
    for (const char* c = source; *c != '\0'; ++c) {
      std::string text(1, shifted_ ? static_cast<char>(*c - 'a' + 'A') : *c);
      row.push_back({key_kind::CHARACTER, text, text, 1.0f, false, false, false, 0});
    }
    return row;
  };

  std::vector<std::vector<key>> rows;
  rows.push_back(letters(kTop));
  rows.push_back(letters(kMiddle));

  std::vector<key> third;
  third.push_back({key_kind::SHIFT, LV_SYMBOL_UP, "", 1.5f, true, false, true, 0});
  for (key& k : letters(kBottom)) third.push_back(k);
  third.push_back({key_kind::BACKSPACE, LV_SYMBOL_BACKSPACE, "", 1.5f, true, false, true, 0});
  rows.push_back(third);

  std::vector<key> fourth;
  fourth.push_back({key_kind::TO_SYMBOLS, "123*", "", 1.7f, false, false, true, 0});
  fourth.push_back({key_kind::CHARACTER, ",", ",", 1.0f, false, false, false, 0});
  fourth.push_back({key_kind::CHARACTER, "", " ", 5.0f, false, false, false, 0});
  fourth.push_back({key_kind::CHARACTER, ".", ".", 1.0f, false, false, false, 0});
  fourth.push_back({key_kind::ENTER, LV_SYMBOL_NEW_LINE, "", 1.7f, true, true, false, 0});
  rows.push_back(fourth);
  return rows;
}

std::vector<std::vector<keyboard::key>> keyboard::symbol_rows() const {
  auto characters = [](const char* source) {
    std::vector<key> row;
    for (const char* c = source; *c != '\0'; ++c) {
      std::string text(1, *c);
      row.push_back({key_kind::CHARACTER, text, text, 1.0f, false, false, false, 0});
    }
    return row;
  };

  std::vector<std::vector<key>> rows;
  rows.push_back(characters("1234567890"));
  rows.push_back(characters("-/:;()$&@_"));

  std::vector<key> third;
  third.push_back({key_kind::CHARACTER, "#", "#", 1.5f, false, false, false, 0});
  for (key& k : characters("+=%!?*")) third.push_back(k);
  third.push_back({key_kind::BACKSPACE, LV_SYMBOL_BACKSPACE, "", 1.5f, true, false, true, 0});
  rows.push_back(third);

  std::vector<key> fourth;
  fourth.push_back({key_kind::TO_LETTERS, "ABC", "", 1.7f, false, false, true, 0});
  fourth.push_back({key_kind::CHARACTER, ",", ",", 1.0f, false, false, false, 0});
  fourth.push_back({key_kind::CHARACTER, "", " ", 5.0f, false, false, false, 0});
  fourth.push_back({key_kind::CHARACTER, ".", ".", 1.0f, false, false, false, 0});
  fourth.push_back({key_kind::ENTER, LV_SYMBOL_NEW_LINE, "", 1.7f, true, true, false, 0});
  rows.push_back(fourth);
  return rows;
}

std::vector<std::vector<keyboard::key>> keyboard::number_rows() const {
  auto digits = [](const char* source) {
    std::vector<key> row;
    for (const char* c = source; *c != '\0'; ++c) {
      std::string text(1, *c);
      row.push_back({key_kind::CHARACTER, text, text, 1.0f, false, false, false, 0});
    }
    return row;
  };

  std::vector<std::vector<key>> rows;
  rows.push_back(digits("123"));
  rows.push_back(digits("456"));
  rows.push_back(digits("789"));

  std::vector<key> fourth;
  fourth.push_back({key_kind::BACKSPACE, LV_SYMBOL_BACKSPACE, "", 1.0f, true, false, true, 0});
  fourth.push_back({key_kind::CHARACTER, "0", "0", 1.0f, false, false, false, 0});
  fourth.push_back({key_kind::ENTER, LV_SYMBOL_NEW_LINE, "", 1.0f, true, true, false, 0});
  rows.push_back(fourth);
  return rows;
}

lv_obj_t* keyboard::create(const keyboard_options& options) {
  if (options.parent == nullptr || options.metrics == nullptr) return nullptr;
  metrics_ = options.metrics;
  strings_ = options.strings;
  textarea_ = options.textarea;
  layout_ = options.layout;
  shell_row_ = options.shell_row;
  // Start lowercase: a field that is not a sentence is the common case here.
  shifted_ = false;
  accept_callback_ = options.accept_callback;
  hidden_callback_ = options.hidden_callback;
  shown_callback_ = options.shown_callback;
  key_callback_ = options.key_callback;
  user_data_ = options.user_data;

  const geometry g = measure(*metrics_, layout_);
  root_ = lv_obj_create(options.parent);
  lv_obj_set_size(root_, metrics_->width, keyboard_height(*metrics_, layout_, shell_row_));
  lv_obj_set_style_pad_all(root_, g.side_pad, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(root_, g.side_pad + keyboard_lift(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(root_, g.gap, LV_PART_MAIN);
  lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root_, 0, LV_PART_MAIN);
  gui2_core::set_surface_style(root_, lv_color_hex(kPanel));
  gui2_core::disable_scrolling(root_);
  lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  prepare_field(textarea_,
                metrics_ != nullptr ? metrics_->primary_text : lv_color_hex(0xFFFFFF));
  blink_field_is(textarea_);

  rows_ = root_;
  build_rows();
  if (options.start_hidden) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  return root_;
}

void keyboard::focus_event_cb(lv_event_t* event) {
  auto* self = static_cast<keyboard*>(lv_event_get_user_data(event));
  auto* textarea = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (self == nullptr || textarea == nullptr) return;
  self->textarea_ = textarea;
  blink_field_is(textarea);
  self->show();
}

void keyboard::bind(lv_obj_t* textarea) {
  if (textarea == nullptr) return;
  prepare_field(textarea, metrics_ != nullptr ? metrics_->primary_text : lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(textarea, focus_event_cb, LV_EVENT_CLICKED, this);
}

void keyboard::clear_rows() {
  if (rows_ != nullptr) lv_obj_clean(rows_);
  keys_.clear();
}

lv_obj_t* keyboard::add_key(lv_obj_t* parent, const key& definition, int width, int height,
                            int radius, bool inside_group) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  // The default theme grows a pressed button by a few pixels while the corner
  // radius stays where it was, so the key reads as a squared-off block a size
  // larger than its neighbours. The colour change is the press feedback.
  lv_obj_set_style_transform_width(button, 0, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_PART_MAIN | LV_STATE_PRESSED);

  const uint32_t fill = definition.accent    ? kAccent
                        : definition.subdued ? kKeySubdued
                                             : kKey;
  if (inside_group) {
    // The group already draws the background; the keys only take the press.
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  } else {
    lv_obj_set_style_bg_color(button, lv_color_hex(fill), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(definition.accent ? kAccent : kKeyPressed),
                              LV_PART_MAIN | LV_STATE_PRESSED);
  }
  if (definition.kind == key_kind::CARET) {
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, definition.label.c_str());
  lv_obj_center(label);
  lv_obj_set_style_text_color(
      label,
      definition.kind == key_kind::CARET ? metrics_->secondary_text : lv_color_white(),
      LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  if (definition.symbol_font) {
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label, -height / 8, LV_PART_MAIN);
  } else {
    // Only the single-glyph caps get the larger size. A word like Copy would
    // not fit its key at that size.
    const bool one_glyph = definition.label.size() == 1;
    const lv_font_t* font = one_glyph && metrics_->keyboard_font != nullptr
                                ? metrics_->keyboard_font
                                : metrics_->text_font;
    if (font != nullptr) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }

  lv_obj_add_event_cb(button, key_event_cb, LV_EVENT_CLICKED, this);
  lv_obj_set_user_data(button, const_cast<key*>(&definition));
  return button;
}

void keyboard::build_rows() {
  if (rows_ == nullptr || metrics_ == nullptr) return;
  clear_rows();

  const geometry g = measure(*metrics_, layout_, shell_row_);
  std::vector<std::vector<key>> rows;
  rows.push_back(function_row());
  if (shell_row_) rows.push_back(shell_row());
  for (auto& row : layout_ == keyboard_layout::LETTERS   ? letter_rows()
                   : layout_ == keyboard_layout::SYMBOLS ? symbol_rows()
                                                         : number_rows())
    rows.push_back(row);

  size_t total = 0;
  for (const auto& row : rows) total += row.size();
  keys_.reserve(total);
  for (const auto& row : rows)
    for (const key& k : row) keys_.push_back(k);

  const int usable_width = metrics_->width - g.side_pad * 2;
  // The home row sits inside the row above it, the way every phone keyboard
  // draws it.
  const int home_row_inset =
      layout_ == keyboard_layout::LETTERS ? (usable_width - 9 * g.gap) / 20 : 0;

  size_t index = 0;
  for (size_t r = 0; r < rows.size(); ++r) {
    const int height = r == 0 ? g.function_height : g.key_height;
    const int radius = r == 0 ? height / 2 : height * 22 / 100;
    // The shell row pushes the letter rows down by one.
    const int inset = r == (shell_row_ ? 3u : 2u) ? home_row_inset : 0;
    lv_obj_t* row_object = add_row(height, g.gap);
    if (inset > 0) {
      lv_obj_set_style_pad_left(row_object, inset, LV_PART_MAIN);
      lv_obj_set_style_pad_right(row_object, inset, LV_PART_MAIN);
    }

    float weight = 0.0f;
    for (const key& k : rows[r]) weight += k.weight;
    // Grouped keys sit on one background, so the gaps between them are gone.
    int gaps = -1;
    for (size_t i = 0; i < rows[r].size(); ++i) {
      const bool merged = i > 0 && rows[r][i].group != 0 && rows[r][i].group == rows[r][i - 1].group;
      if (!merged) ++gaps;
    }
    const int available = usable_width - gaps * g.gap - inset * 2;

    for (size_t i = 0; i < rows[r].size();) {
      const int group = rows[r][i].group;
      size_t run_end = i + 1;
      if (group != 0)
        while (run_end < rows[r].size() && rows[r][run_end].group == group) ++run_end;

      if (group == 0) {
        const key& definition = keys_[index];
        const int width = static_cast<int>(available * definition.weight / weight);
        add_key(row_object, definition, width, height, radius, false);
        ++index;
      } else {
        float run_weight = 0.0f;
        for (size_t k = i; k < run_end; ++k) run_weight += rows[r][k].weight;
        const int run_width = static_cast<int>(available * run_weight / weight);

        lv_obj_t* group_object = lv_obj_create(row_object);
        lv_obj_set_size(group_object, run_width, height);
        lv_obj_set_style_radius(group_object, radius, LV_PART_MAIN);
        lv_obj_set_style_border_width(group_object, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(group_object, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(group_object, 0, LV_PART_MAIN);
        gui2_core::set_surface_style(group_object, lv_color_hex(kKeySubdued));
        gui2_core::disable_scrolling(group_object);
        lv_obj_set_flex_flow(group_object, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(group_object, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        for (size_t k = i; k < run_end; ++k, ++index) {
          const key& definition = keys_[index];
          const int width = static_cast<int>(run_width * definition.weight / run_weight);
          add_key(group_object, definition, width, height, radius, true);
        }
      }
      i = run_end;
    }
  }
}

lv_obj_t* keyboard::add_row(int height, int gap) {
  lv_obj_t* row = lv_obj_create(rows_);
  lv_obj_set_size(row, LV_PCT(100), height);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, gap, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  gui2_core::disable_scrolling(row);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

void keyboard::key_event_cb(lv_event_t* event) {
  auto* self = static_cast<keyboard*>(lv_event_get_user_data(event));
  auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (self == nullptr || button == nullptr) return;
  const auto* definition = static_cast<const key*>(lv_obj_get_user_data(button));
  if (definition == nullptr) return;
  self->handle(*definition);
}

void keyboard::handle(const key& definition) {
  if (key_callback_ != nullptr) key_callback_(user_data_);
  // Any key restarts the blink lit, so the caret is never mid-blink-off at the
  // moment it moves.
  if (definition.kind != key_kind::HIDE) blink_field_is(textarea_);

  switch (definition.kind) {
    case key_kind::CHARACTER:
      if (textarea_ != nullptr) lv_textarea_add_text(textarea_, definition.text.c_str());
      break;
    case key_kind::SHIFT:
      shifted_ = !shifted_;
      build_rows();
      break;
    case key_kind::BACKSPACE:
      if (textarea_ != nullptr) lv_textarea_delete_char(textarea_);
      break;
    case key_kind::ENTER:
      if (accept_callback_ != nullptr)
        accept_callback_(user_data_);
      else
        hide();
      break;
    case key_kind::TO_SYMBOLS:
      layout_ = keyboard_layout::SYMBOLS;
      build_rows();
      break;
    case key_kind::TO_LETTERS:
      // The case the user left the letters in is the case they come back to.
      layout_ = keyboard_layout::LETTERS;
      build_rows();
      break;
    case key_kind::HIDE:
      hide();
      break;
    case key_kind::CURSOR_LEFT:
      if (textarea_ != nullptr) lv_textarea_cursor_left(textarea_);
      break;
    case key_kind::CURSOR_RIGHT:
      if (textarea_ != nullptr) lv_textarea_cursor_right(textarea_);
      break;
    case key_kind::COPY:
      if (textarea_ != nullptr) {
        const char* text = lv_textarea_get_text(textarea_);
        if (text != nullptr) {
          size_t from = 0;
          size_t to = 0;
          // A selection means "copy this much"; without one, take the lot.
          if (selected_range(textarea_, text, &from, &to))
            clipboard() = std::string(text + from, to - from);
          else
            clipboard() = text;
        }
      }
      break;
    case key_kind::PASTE:
      if (textarea_ != nullptr && !clipboard().empty())
        lv_textarea_add_text(textarea_, clipboard().c_str());
      break;
    case key_kind::CARET:
      break;
  }
}

void keyboard::detach() {
  // The timer outlives the page otherwise, and its field is already gone.
  blink_stop();
  root_ = nullptr;
  rows_ = nullptr;
  textarea_ = nullptr;
  keys_.clear();
}

void keyboard::slide_y_cb(void* target, int32_t value) {
  lv_obj_set_y(static_cast<lv_obj_t*>(target), value);
}

void keyboard::hidden_anim_done(lv_anim_t* anim) {
  auto* self = static_cast<keyboard*>(lv_anim_get_user_data(anim));
  if (self == nullptr || self->root_ == nullptr) return;
  lv_obj_add_flag(self->root_, LV_OBJ_FLAG_HIDDEN);
  // Straight back to the stored resting place, not derived from where the
  // slide happened to stop. Leaving it parked off-screen makes the next show()
  // read that as the resting place and slide to nowhere.
  lv_obj_set_y(self->root_, self->resting_y_);
}

void keyboard::show() {
  if (root_ == nullptr) return;
  // The page sets the keyboard's position after create(); its y only leaves
  // that resting place while a slide is running.
  if (lv_anim_get(root_, slide_y_cb) == nullptr) resting_y_ = lv_obj_get_y(root_);
  lv_anim_delete(root_, slide_y_cb);
  const bool was_hidden = lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);

  // Off-screen when it was hidden, otherwise wherever an interrupted slide left
  // it. Returning early here is what used to strand the keyboard halfway when
  // it was shown and hidden in quick succession.
  const int from = was_hidden ? resting_y_ + lv_obj_get_height(root_) : lv_obj_get_y(root_);
  if (from == resting_y_) return;
  lv_obj_set_y(root_, from);
  if (shown_callback_ != nullptr) shown_callback_(user_data_);

  // Slide up rather than appearing all at once.
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, root_);
  lv_anim_set_exec_cb(&anim, slide_y_cb);
  lv_anim_set_values(&anim, from, resting_y_);
  lv_anim_set_duration(&anim, 180);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  lv_anim_start(&anim);
}

void keyboard::hide() {
  if (root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
    if (lv_anim_get(root_, slide_y_cb) == nullptr) resting_y_ = lv_obj_get_y(root_);
    lv_anim_delete(root_, slide_y_cb);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, root_);
    lv_anim_set_exec_cb(&anim, slide_y_cb);
    lv_anim_set_values(&anim, lv_obj_get_y(root_), resting_y_ + lv_obj_get_height(root_));
    lv_anim_set_duration(&anim, 160);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    // The flag goes on only once it is off-screen, or it vanishes mid-slide.
    lv_anim_set_user_data(&anim, this);
    lv_anim_set_completed_cb(&anim, hidden_anim_done);
    lv_anim_start(&anim);
  }
  if (hidden_callback_ != nullptr) hidden_callback_(user_data_);
}

void keyboard::dismiss() {
  blink_stop();
  if (root_ != nullptr) {
    lv_anim_delete(root_, slide_y_cb);
    lv_obj_delete(root_);
  }
  root_ = nullptr;
  rows_ = nullptr;
  textarea_ = nullptr;
  keys_.clear();
}

bool keyboard::visible() const {
  return root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace gui2_components
