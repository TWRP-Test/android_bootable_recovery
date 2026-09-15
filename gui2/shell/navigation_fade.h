#ifndef GUI2_SHELL_NAVIGATION_FADE_H
#define GUI2_SHELL_NAVIGATION_FADE_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

lv_obj_t* create_navigation_fade(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics);

}  // namespace gui2_shell

#endif
