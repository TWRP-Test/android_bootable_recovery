#include "shell/page_host.h"

#include <algorithm>

#include "core/ui_helpers.h"
#include "shell/navigation_fade.h"

namespace gui2_shell {

void page_host::initialize(lv_obj_t* layer, const gui2_core::ui_metrics& metrics) {
  layer_ = layer;
  metrics_ = &metrics;
  content_ = nullptr;
  current_page_ = nullptr;
  previous_page_ = nullptr;
  input_blocker_ = nullptr;
  transition_ = gui2_core::page_transition::NONE;
  transition_active_ = false;
}

page_scaffold_result page_host::build(const char* title, const char* summary, int bottom_reserved,
                                      gui2_core::page_transition transition) {
  page_scaffold_result result;
  if (layer_ == nullptr || metrics_ == nullptr) return result;

  // A new page gets its own root so the old page can remain visible while the
  // transition runs. The shell's status bar and bottom navigation are outside
  // this root and therefore remain fixed in place.
  stop_transition();
  lv_obj_t* new_page = lv_obj_create(layer_);
  lv_obj_set_pos(new_page, 0, 0);
  lv_obj_set_size(new_page, metrics_->width,
                  std::max(1, metrics_->height - metrics_->status_height));
  gui2_core::set_surface_style(new_page, metrics_->background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(new_page, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(new_page);

  result = build_page_scaffold(new_page, *metrics_, title, summary, bottom_reserved);
  create_navigation_fade(new_page, *metrics_);
  content_ = result.content;

  if (current_page_ == nullptr) {
    current_page_ = new_page;
    return result;
  }

  previous_page_ = current_page_;
  current_page_ = new_page;
  transition_ = transition;
  if (transition == gui2_core::page_transition::NONE ||
      transition == gui2_core::page_transition::REPLACE) {
    lv_obj_delete(previous_page_);
    previous_page_ = nullptr;
    return result;
  }

  const int width = metrics_->width;
  if (transition == gui2_core::page_transition::POP) {
    lv_obj_set_x(new_page, -width);
  } else {
    lv_obj_set_x(new_page, width);
  }

  // Keep both pages non-interactive until the new page is settled. The
  // blocker is inside page_layer, so the persistent navigation remains usable.
  input_blocker_ = lv_obj_create(layer_);
  lv_obj_set_pos(input_blocker_, 0, 0);
  lv_obj_set_size(input_blocker_, metrics_->width,
                  std::max(1, metrics_->height - metrics_->status_height));
  gui2_core::set_surface_style(input_blocker_, metrics_->background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(input_blocker_, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(input_blocker_);
  lv_obj_add_flag(input_blocker_, LV_OBJ_FLAG_CLICKABLE);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, 0, 1000);
  lv_anim_set_duration(
      &animation, std::clamp(gui2_core::ui_px(220), gui2_core::ui_px(160), gui2_core::ui_px(280)));
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, animation_exec);
  lv_anim_set_completed_cb(&animation, animation_ready);
  transition_active_ = true;
  lv_anim_start(&animation);
  return result;
}

void page_host::clear() {
  stop_transition();
  if (layer_ != nullptr) lv_obj_clean(layer_);
  content_ = nullptr;
  current_page_ = nullptr;
  previous_page_ = nullptr;
  input_blocker_ = nullptr;
  layer_ = nullptr;
  metrics_ = nullptr;
}

void page_host::animation_exec(void* object, int32_t progress) {
  auto* host = static_cast<page_host*>(object);
  if (host == nullptr || host->current_page_ == nullptr || host->previous_page_ == nullptr) return;

  const int width = host->metrics_ == nullptr ? 0 : host->metrics_->width;
  const int distance = static_cast<int>((static_cast<int64_t>(width) * progress) / 1000);
  if (host->transition_ == gui2_core::page_transition::POP) {
    lv_obj_set_x(host->previous_page_, distance);
    lv_obj_set_x(host->current_page_, distance - width);
  } else {
    lv_obj_set_x(host->previous_page_, -(distance / 4));
    lv_obj_set_x(host->current_page_, width - distance);
  }
}

void page_host::animation_ready(lv_anim_t* animation) {
  if (animation == nullptr) return;
  auto* host = static_cast<page_host*>(lv_anim_get_user_data(animation));
  if (host != nullptr) host->finish_transition();
}

void page_host::finish_transition() {
  if (!transition_active_) return;
  transition_active_ = false;
  if (input_blocker_ != nullptr) {
    lv_obj_delete(input_blocker_);
    input_blocker_ = nullptr;
  }
  if (previous_page_ != nullptr) {
    lv_obj_delete(previous_page_);
    previous_page_ = nullptr;
  }
  if (current_page_ != nullptr) lv_obj_set_x(current_page_, 0);
  transition_ = gui2_core::page_transition::NONE;
}

void page_host::stop_transition() {
  lv_anim_delete(this, animation_exec);
  if (input_blocker_ != nullptr) {
    lv_obj_delete(input_blocker_);
    input_blocker_ = nullptr;
  }
  if (previous_page_ != nullptr) {
    lv_obj_delete(previous_page_);
    previous_page_ = nullptr;
  }
  if (current_page_ != nullptr) lv_obj_set_x(current_page_, 0);
  transition_active_ = false;
  transition_ = gui2_core::page_transition::NONE;
}

}  // namespace gui2_shell
