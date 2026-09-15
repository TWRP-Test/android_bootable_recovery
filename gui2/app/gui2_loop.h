#ifndef GUI2_APP_GUI2_LOOP_H
#define GUI2_APP_GUI2_LOOP_H

#include "backend/screen_backend.h"
#include "gui2_input.h"
#include "lvgl.h"

namespace gui2_app {

struct loop_callbacks {
  bool (*should_exit)(void* user_data) = nullptr;
  void (*on_tick)(void* user_data, uint64_t monotonic_ms) = nullptr;
  void (*on_key_action)(void* user_data, gui2_key_action action) = nullptr;
  void (*on_activity)(void* user_data, bool was_screen_off) = nullptr;
  void (*on_wheel)(void* user_data, int amount) = nullptr;
  void (*after_present)(void* user_data) = nullptr;
};

void run_gui2_loop(gui2_backend::screen_backend* screen, lv_indev_t* pointer_indev, void* user_data,
                   const loop_callbacks& callbacks);

}  // namespace gui2_app

#endif
