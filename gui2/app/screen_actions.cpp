#include "app/screen_actions.h"

#include "lvgl.h"

#include <time.h>

namespace gui2_app {

void screen_actions::initialize(gui2_backend::screen_backend* screen,
                                gui2_shell::screen_feedback* feedback,
                                void (*before_screen_off)(void*), void* callback_user_data) {
  screen_ = screen;
  feedback_ = feedback;
  before_screen_off_ = before_screen_off;
  callback_user_data_ = callback_user_data;
  screen_->set_before_screen_off_callback(before_screen_off_, callback_user_data_);
  pending_screenshot_ = false;
  pending_screen_off_ = false;
}

void screen_actions::request_screenshot() {
  pending_screenshot_ = true;
}

void screen_actions::request_screen_off() {
  pending_screen_off_ = true;
}

bool screen_actions::toggle_screen() {
  if (screen_ == nullptr) return false;
  if (screen_->is_screen_off()) {
    if (screen_->screen_on()) {
      gui2_input_set_screen_off(false);
      lv_obj_invalidate(lv_screen_active());
      return true;
    }
  } else {
    request_screen_off();
  }
  return false;
}

void screen_actions::process_after_present() {
  if (screen_ == nullptr) return;
  if (pending_screenshot_) {
    pending_screenshot_ = false;
    const gui2_backend::capture_result result = screen_->save_screenshot();
    if (result.success && feedback_ != nullptr) {
      timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      const uint64_t now_ms = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
      feedback_->show_screenshot(now_ms);
    }
    if (screenshot_result_ != nullptr) screenshot_result_(callback_user_data_, result);
  }
  if (pending_screen_off_) {
    pending_screen_off_ = false;
    if (screen_->screen_off()) gui2_input_set_screen_off(true);
  }
}

void screen_actions::reset() {
  if (screen_ != nullptr) screen_->set_before_screen_off_callback(nullptr, nullptr);
  screen_ = nullptr;
  feedback_ = nullptr;
  before_screen_off_ = nullptr;
  callback_user_data_ = nullptr;
  pending_screenshot_ = false;
  pending_screen_off_ = false;
}

}  // namespace gui2_app
