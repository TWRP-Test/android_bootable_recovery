#ifndef GUI2_COMPONENTS_ICON_H
#define GUI2_COMPONENTS_ICON_H

#include "lvgl.h"

namespace gui2_components {

lv_obj_t* create_svg_image(lv_obj_t* parent, const lv_image_dsc_t* source, int width, int height);

// Scales symbol/icon text around the actual object center. The caller owns
// the scale policy; this component only applies it to the LVGL object.
void scale_icon_font(lv_obj_t* object, float scale);

int action_icon_art_size(int color_block_size);

}  // namespace gui2_components

#endif
