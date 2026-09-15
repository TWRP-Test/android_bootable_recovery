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

 private:
  lv_font_t* text_ = nullptr;
  lv_font_t* status_ = nullptr;
  lv_font_t* brand_ = nullptr;
};

}  // namespace gui2_theme

#endif
