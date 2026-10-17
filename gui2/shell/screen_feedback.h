#ifndef GUI2_SHELL_SCREEN_FEEDBACK_H
#define GUI2_SHELL_SCREEN_FEEDBACK_H

#include "lvgl.h"

namespace gui2_shell {

class screen_feedback {
 public:
  void attach(lv_obj_t* screenshot_flash);
  void show_screenshot(uint64_t now_ms);
  void update(uint64_t now_ms);
  void reset();

 private:
  lv_obj_t* screenshot_flash_ = nullptr;
  uint64_t until_ms_ = 0;
};

}  // namespace gui2_shell

#endif
