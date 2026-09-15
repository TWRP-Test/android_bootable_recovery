#ifndef GUI2_SHELL_CONFIRM_DIALOG_H
#define GUI2_SHELL_CONFIRM_DIALOG_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

struct confirm_dialog_options {
  const gui2_core::ui_metrics* metrics = nullptr;
  const char* title = nullptr;
  const char* body = nullptr;
  const char* cancel = nullptr;
  const char* confirm = nullptr;
  lv_event_cb_t cancel_callback = nullptr;
  lv_event_cb_t confirm_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

lv_obj_t* create_confirm_dialog(const confirm_dialog_options& options);

}  // namespace gui2_shell

#endif
