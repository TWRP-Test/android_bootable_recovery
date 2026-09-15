#ifndef GUI2_APP_SCREEN_ACTIONS_H
#define GUI2_APP_SCREEN_ACTIONS_H

#include "backend/screen_backend.h"
#include "gui2_input.h"
#include "shell/screen_feedback.h"

namespace gui2_app {

class screen_actions {
 public:
  void initialize(gui2_backend::screen_backend* screen, gui2_shell::screen_feedback* feedback,
                  void (*before_screen_off)(void*), void* callback_user_data);
  void request_screenshot();
  void request_screen_off();
  bool toggle_screen();
  void process_after_present();
  void reset();

 private:
  gui2_backend::screen_backend* screen_ = nullptr;
  gui2_shell::screen_feedback* feedback_ = nullptr;
  void (*before_screen_off_)(void*) = nullptr;
  void* callback_user_data_ = nullptr;
  bool pending_screenshot_ = false;
  bool pending_screen_off_ = false;
};

}  // namespace gui2_app

#endif
