#ifndef GUI2_CORE_UI_HELPERS_H
#define GUI2_CORE_UI_HELPERS_H

#include "lvgl.h"

namespace gui2_core {

// Common LVGL surface setup used by shell, pages, and reusable components.
void set_surface_style(lv_obj_t* object, lv_color_t color,
                       lv_opa_t opa = LV_OPA_COVER);

// Containers are non-scrollable by default. The page content viewport is the
// explicit exception and enables scrolling at its call site.
void disable_scrolling(lv_obj_t* object);

}  // namespace gui2_core

#endif
