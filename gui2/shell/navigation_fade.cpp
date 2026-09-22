#include "shell/navigation_fade.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_shell {

namespace {

// The latch lives in the object's user data: a plain "is it on screen right
// now" bit, so a scroll event that changes nothing starts no animation.
bool fade_is_shown(lv_obj_t* fade) {
  return lv_obj_get_user_data(fade) != nullptr;
}

void scroll_event_cb(lv_event_t* event) {
  update_navigation_fade(static_cast<lv_obj_t*>(lv_event_get_user_data(event)),
                         static_cast<lv_obj_t*>(lv_event_get_current_target(event)));
}

}  // namespace

void update_navigation_fade(lv_obj_t* fade, lv_obj_t* content, bool animate) {
  if (fade == nullptr || content == nullptr) return;
  // A couple of pixels of slack: elastic scrolling overshoots, and a list that
  // ends exactly at the edge should not flicker.
  const bool wanted = lv_obj_get_scroll_bottom(content) > gui2_core::ui_px(4);
  if (!animate) {
    // Also overrides a fade a scroll event started while the page was built.
    lv_anim_delete(fade, nullptr);
    lv_obj_set_style_opa(fade, wanted ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_user_data(fade, wanted ? fade : nullptr);
    return;
  }
  if (wanted == fade_is_shown(fade)) return;
  lv_obj_set_user_data(fade, wanted ? fade : nullptr);
  if (wanted)
    lv_obj_fade_in(fade, 140, 0);
  else
    lv_obj_fade_out(fade, 140, 0);
}

void bind_navigation_fade(lv_obj_t* fade, lv_obj_t* content) {
  if (fade == nullptr || content == nullptr) return;
  lv_obj_add_event_cb(content, scroll_event_cb, LV_EVENT_SCROLL, fade);
  lv_obj_add_event_cb(content, scroll_event_cb, LV_EVENT_SCROLL_END, fade);
}

lv_obj_t* create_navigation_fade(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics,
                                 int reserved) {
  if (page_layer == nullptr) return nullptr;
  const int overhang = std::max(gui2_core::ui_px(24), metrics.outer_margin / 2);
  // The run is where the content actually fades; it sits directly above
  // whatever the page reserved, so the control below it always stands on solid
  // black while the list above it stays readable to the last moment.
  // A page whose only obstruction is the navigation bar needs a short fade; the
  // long one is for pages with a control floating above it.
  const int run = reserved > gui2_core::navigation_safe_area()
                      ? gui2_core::single_line_card_height()
                      : gui2_core::single_line_card_height() / 2;
  const int fade_height = std::max(metrics.nav_height, reserved) + run;
  const int fade_top = std::max(0, metrics.height - metrics.status_height - fade_height);
  lv_obj_t* fade = lv_obj_create(page_layer);
  lv_obj_set_pos(fade, 0, fade_top);
  // Stop short of the scrollbar column, or the bar disappears into the
  // gradient exactly where a long page needs it most.
  const int scrollbar_column = gui2_core::ui_px(14);
  lv_obj_set_size(fade, std::max(1, metrics.width - scrollbar_column), fade_height);
  gui2_core::set_surface_style(fade, lv_color_hex(0x000000), LV_OPA_COVER);
  lv_obj_set_style_radius(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_main_opa(fade, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(fade, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_opa(fade, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, LV_PART_MAIN);
  // Transparent at the top, fully black where the navigation bar starts. The
  // floating control sits inside the run, so content behind it is already
  // dimming without being cut off early.
  const int solid_from = std::max(0, fade_height - metrics.nav_height);
  lv_obj_set_style_bg_main_stop(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_stop(
      fade, static_cast<int32_t>(fade_height > 0 ? 255 * solid_from / fade_height : 255),
      LV_PART_MAIN);
  lv_obj_clear_flag(fade, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(fade);
  // Starts invisible; settle() turns it on once the page has been measured.
  lv_obj_set_style_opa(fade, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_user_data(fade, nullptr);
  return fade;
}

}  // namespace gui2_shell
