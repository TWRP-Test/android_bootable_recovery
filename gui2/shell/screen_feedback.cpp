#include "shell/screen_feedback.h"

namespace gui2_shell {

void screen_feedback::attach(lv_obj_t* screenshot_flash) {
  screenshot_flash_ = screenshot_flash;
  until_ms_ = 0;
}

void screen_feedback::show_screenshot(uint64_t now_ms) {
  if (screenshot_flash_ == nullptr) return;
  lv_obj_set_hidden(screenshot_flash_, false);
  lv_obj_invalidate(screenshot_flash_);
  until_ms_ = now_ms + 70;
}

void screen_feedback::update(uint64_t now_ms) {
  if (screenshot_flash_ == nullptr || until_ms_ == 0) return;
  if (now_ms >= until_ms_) {
    lv_obj_set_hidden(screenshot_flash_, true);
    until_ms_ = 0;
  }
}

void screen_feedback::reset() {
  screenshot_flash_ = nullptr;
  until_ms_ = 0;
}

}  // namespace gui2_shell
