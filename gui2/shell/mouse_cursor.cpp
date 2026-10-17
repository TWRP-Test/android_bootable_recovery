#include "shell/mouse_cursor.h"

#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"
#include "gui2_svg_cache.h"

namespace gui2_shell {

lv_obj_t* create_mouse_cursor(lv_indev_t* pointer_indev, bool has_mouse,
                              const gui2_core::ui_metrics& metrics) {
  if (pointer_indev == nullptr || !has_mouse) return nullptr;
  lv_obj_t* cursor = lv_image_create(lv_layer_sys());
  if (cursor == nullptr) return nullptr;
  const int size = gui2_core::ui_px(48);
  lv_obj_set_size(cursor, size, size);
  lv_image_set_src(cursor, gui2_svg_get_raster(&kGui2IconCursor, size, size));
  lv_image_set_inner_align(cursor, LV_IMAGE_ALIGN_CONTAIN);
  gui2_core::disable_scrolling(cursor);
  lv_indev_set_cursor(pointer_indev, cursor);
  (void)metrics;
  return cursor;
}

}  // namespace gui2_shell
