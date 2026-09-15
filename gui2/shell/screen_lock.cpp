#include "shell/screen_lock.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_shell {

void screen_lock::create(const gui2_core::ui_metrics& metrics, const char* lock_text,
                         const char* swipe_text, void (*unlock_callback)(void*), void* user_data) {
  metrics_ = &metrics;
  root_ = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root_, metrics.width, metrics.height);
  lv_obj_set_pos(root_, 0, 0);
  gui2_core::set_surface_style(root_, lv_color_black(), LV_OPA_70);
  lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(root_);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  const int lock_size =
      std::clamp(gui2_core::ui_px(880), gui2_core::ui_px(560),
                 std::min(metrics.width - gui2_core::ui_px(48), metrics.height * 34 / 100));
  lv_obj_t* lock = gui2_components::create_svg_image(root_, &kGui2IconLock, lock_size, lock_size);
  lv_obj_align(lock, LV_ALIGN_TOP_MID, 0, metrics.height * 26 / 100);

  lv_obj_t* lock_label = lv_label_create(root_);
  lv_label_set_text(lock_label, lock_text == nullptr ? "" : lock_text);
  lv_obj_set_style_text_color(lock_label, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(lock_label, metrics.status_font, LV_PART_MAIN);
  lv_obj_align(lock_label, LV_ALIGN_TOP_MID, 0,
               metrics.height * 26 / 100 + lock_size + gui2_core::ui_px(12));

  const int track_width = std::clamp(metrics.width * 86 / 100, gui2_core::ui_px(420),
                                     metrics.width - gui2_core::ui_px(48));
  const int track_height =
      std::clamp(gui2_core::ui_px(150), gui2_core::ui_px(100), gui2_core::ui_px(180));
  slider_.create(root_, metrics, (metrics.width - track_width) / 2, metrics.height * 68 / 100,
                 track_width, track_height, swipe_text, unlock_callback, user_data);
  hide();
}

void screen_lock::show() {
  if (root_ != nullptr) {
    slider_.reset();
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }
}

void screen_lock::hide() {
  if (root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

bool screen_lock::visible() const {
  return root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void screen_lock::reset() {
  root_ = nullptr;
  metrics_ = nullptr;
  slider_.reset();
}

}  // namespace gui2_shell
