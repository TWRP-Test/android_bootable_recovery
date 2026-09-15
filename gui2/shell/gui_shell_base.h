#ifndef GUI2_SHELL_GUI_SHELL_BASE_H
#define GUI2_SHELL_GUI_SHELL_BASE_H

#include "core/ui_metrics.h"
#include "lvgl.h"
#include "shell/status_bar.h"

namespace gui2_shell {

struct gui_shell_base_options {
  lv_obj_t* screen = nullptr;
  const lv_font_t* text_font = nullptr;
  const lv_font_t* status_font = nullptr;
  const lv_font_t* brand_font = nullptr;
  const char* recording_text = nullptr;
  lv_event_cb_t status_gesture_callback = nullptr;
};

struct gui_shell_base_view {
  status_bar_view status;
  lv_obj_t* page_layer = nullptr;
};

gui_shell_base_view create_gui_shell_base(const gui_shell_base_options& options);

}  // namespace gui2_shell

#endif
