#ifndef GUI2_THEME_FONT_MANAGER_H
#define GUI2_THEME_FONT_MANAGER_H

#include "lvgl.h"

namespace gui2_theme {

// Owns runtime TinyTTF fonts for the lifetime of the LVGL object tree.
// Call shutdown only after all objects using these fonts have been destroyed.
class font_manager {
 public:
  bool initialize(float scale);
  void shutdown();

  lv_font_t* text() const {
    return text_;
  }
  lv_font_t* status() const {
    return status_;
  }
  lv_font_t* brand() const {
    return brand_;
  }
  // Key caps carry a single glyph, so they read better a size up from body
  // text.
  lv_font_t* keyboard() const {
    return keyboard_;
  }
  lv_font_t* console(int index) const;
  lv_font_t* terminal() const { return terminal_; }
  // Terminal owns a separate mutable font; callers refresh its labels after resizing.
  int resize_terminal(int size);
  int terminal_size() const { return terminal_size_; }
  static int console_count() {
    return kConsoleSizeCount;
  }

 private:
  static constexpr int kConsoleSizeCount = 3;

  lv_font_t* text_ = nullptr;
  lv_font_t* status_ = nullptr;
  lv_font_t* brand_ = nullptr;
  lv_font_t* keyboard_ = nullptr;
  lv_font_t* terminal_ = nullptr;
  float scale_ = 1.0f;
  int terminal_size_ = 34;
  lv_font_t* console_[kConsoleSizeCount] = {};
};

}  // namespace gui2_theme

#endif
