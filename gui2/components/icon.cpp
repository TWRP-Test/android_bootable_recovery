#include "components/icon.h"

#include <algorithm>
#include <cmath>

#include "gui2_svg_cache.h"

namespace gui2_components {

lv_obj_t* create_svg_image(lv_obj_t* parent, const lv_image_dsc_t* source, int width, int height) {
  if (parent == nullptr || source == nullptr) return nullptr;
  lv_obj_t* image = lv_image_create(parent);
  if (image == nullptr) return nullptr;
  lv_image_set_src(image, gui2_svg_get_raster(source, width, height));
  lv_obj_set_size(image, std::max(1, width), std::max(1, height));
  lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
  lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
  return image;
}

void scale_icon_font(lv_obj_t* object, float scale) {
  if (object == nullptr) return;
  lv_obj_update_layout(object);
  lv_obj_set_style_transform_pivot_x(object, lv_obj_get_width(object) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(object, lv_obj_get_height(object) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_scale(object, static_cast<int32_t>(std::lround(scale * LV_SCALE_NONE)),
                                   LV_PART_MAIN);
}

int action_icon_art_size(int color_block_size) {
  // Keep the vector artwork visually inset from the colored rounded square.
  return std::max(1, color_block_size * 82 / 100);
}

}  // namespace gui2_components
