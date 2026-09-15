#ifndef GUI2_SHELL_MOUSE_CURSOR_H
#define GUI2_SHELL_MOUSE_CURSOR_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

lv_obj_t* create_mouse_cursor(lv_indev_t* pointer_indev, bool has_mouse,
                              const gui2_core::ui_metrics& metrics);

}  // namespace gui2_shell

#endif
