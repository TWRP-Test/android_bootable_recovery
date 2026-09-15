#include "shell/navigation_fade.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_shell {

lv_obj_t* create_navigation_fade(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics) {
  if (page_layer == nullptr) return nullptr;
  const int overhang = std::max(gui2_core::ui_px(24), metrics.outer_margin / 2);
  const int fade_height = metrics.nav_height + overhang;
  const int fade_top = std::max(0, metrics.height - metrics.status_height - fade_height);
  lv_obj_t* fade = lv_obj_create(page_layer);
  lv_obj_set_pos(fade, 0, fade_top);
  lv_obj_set_size(fade, metrics.width, fade_height);
  gui2_core::set_surface_style(fade, lv_color_hex(0x000000), LV_OPA_COVER);
  lv_obj_set_style_radius(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_main_opa(fade, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(fade, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_opa(fade, static_cast<lv_opa_t>(190), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_bg_main_stop(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_stop(fade, 255, LV_PART_MAIN);
  lv_obj_clear_flag(fade, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(fade);
  return fade;
}

}  // namespace gui2_shell
