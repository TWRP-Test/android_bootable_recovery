#ifndef GUI2_COMPONENTS_SECTION_LABEL_H
#define GUI2_COMPONENTS_SECTION_LABEL_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

lv_obj_t* create_section_label(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                               const char* text);

}  // namespace gui2_components

#endif
