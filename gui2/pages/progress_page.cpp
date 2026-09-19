#include "pages/progress_page.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;
constexpr uint32_t kDone = 0x18C935;
constexpr uint32_t kFailed = 0xF0443E;

void set_x_cb(void* target, int32_t value) {
  lv_obj_set_x(static_cast<lv_obj_t*>(target), value);
}

void set_width_cb(void* target, int32_t value) {
  lv_obj_set_width(static_cast<lv_obj_t*>(target), value);
}

void set_border_opa_cb(void* target, int32_t value) {
  lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(target), static_cast<lv_opa_t>(value),
                              LV_PART_MAIN);
}

// The legacy indicator pulses its outline while a job runs. Easing both the
// band and the outline is what keeps it from looking stepped.
void start_running_animation(progress_page_view* view) {
  if (view->sweeping || view->bar == nullptr || view->bar_fill == nullptr) return;

  const int width = view->track_width;
  if (width <= 0) return;
  const int band = std::max(1, width / 3);
  lv_anim_delete(view->bar_fill, set_width_cb);
  lv_obj_set_width(view->bar_fill, band);

  lv_anim_t band_animation;
  lv_anim_init(&band_animation);
  lv_anim_set_var(&band_animation, view->bar_fill);
  lv_anim_set_exec_cb(&band_animation, set_x_cb);
  lv_anim_set_values(&band_animation, 0, std::max(0, width - band));
  lv_anim_set_duration(&band_animation, 1000);
  lv_anim_set_reverse_duration(&band_animation, 1000);
  lv_anim_set_path_cb(&band_animation, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&band_animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&band_animation);

  lv_anim_t outline_animation;
  lv_anim_init(&outline_animation);
  lv_anim_set_var(&outline_animation, view->bar);
  lv_anim_set_exec_cb(&outline_animation, set_border_opa_cb);
  lv_anim_set_values(&outline_animation, LV_OPA_20, LV_OPA_COVER);
  lv_anim_set_duration(&outline_animation, 1000);
  lv_anim_set_reverse_duration(&outline_animation, 1000);
  lv_anim_set_path_cb(&outline_animation, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&outline_animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&outline_animation);

  view->sweeping = true;
}

void stop_running_animation(progress_page_view* view) {
  if (!view->sweeping) return;
  if (view->bar_fill != nullptr) {
    lv_anim_delete(view->bar_fill, set_x_cb);
    lv_obj_set_x(view->bar_fill, 0);
  }
  if (view->bar != nullptr) {
    lv_anim_delete(view->bar, set_border_opa_cb);
    lv_obj_set_style_border_opa(view->bar, LV_OPA_COVER, LV_PART_MAIN);
  }
  view->sweeping = false;
}

void animate_width(lv_obj_t* fill, int target) {
  if (fill == nullptr) return;
  const int current = lv_obj_get_width(fill);
  if (current == target) return;

  lv_anim_delete(fill, set_width_cb);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, fill);
  lv_anim_set_exec_cb(&animation, set_width_cb);
  lv_anim_set_values(&animation, current, target);
  lv_anim_set_duration(&animation, 320);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_start(&animation);
}

}  // namespace

progress_page_view build_progress_page(const progress_page_options& options) {
  progress_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  view.subtitle = options.subtitle;
  // Same footprint as the swipe control the user just released.
  const int bar_height =
      std::clamp(gui2_core::ui_px(150), gui2_core::ui_px(100), gui2_core::ui_px(180));
  const int border = std::max(2, gui2_core::ui_px(4));

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.cards_top_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.body);

  console_page_options console_options;
  console_options.content = view.body;
  console_options.metrics = &metrics;
  console_options.empty_text = "";
  console_options.font = options.console_font;
  console_options.self_scrolling = true;
  view.console = build_console_page(console_options);
  if (view.console.body != nullptr) {
    // The console page sizes itself to the whole viewport; give back only what
    // the bar and its gap take.
    const int height =
        std::max(gui2_core::ui_px(300),
                 view.console.minimum_height - bar_height - metrics.cards_top_gap);
    lv_obj_set_pos(view.console.body, 0, 0);
    lv_obj_set_height(view.console.body, height);
    view.console.minimum_height = height;
  }

  view.bar = lv_obj_create(view.body);
  lv_obj_set_size(view.bar, metrics.content_width, bar_height);
  gui2_core::set_surface_style(view.bar,
                               lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 38));
  lv_obj_set_style_radius(view.bar, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.bar, border, LV_PART_MAIN);
  lv_obj_set_style_border_color(view.bar, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_border_opa(view.bar, LV_OPA_COVER, LV_PART_MAIN);
  // Without this the square-ended fill draws past the rounded cap.
  lv_obj_set_style_clip_corner(view.bar, true, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar);

  view.track_width = std::max(0, metrics.content_width - border * 2);
  view.bar_fill = lv_obj_create(view.bar);
  lv_obj_set_size(view.bar_fill, 1, bar_height - border * 2);
  // A one pixel fill pokes out of the rounded end; stay hidden until there is
  // something real to draw.
  lv_obj_add_flag(view.bar_fill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(view.bar_fill, 0, 0);
  gui2_core::set_surface_style(view.bar_fill, lv_color_hex(kAccent));
  lv_obj_set_style_radius(view.bar_fill, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar_fill, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar_fill);
  return view;
}

void update_progress(progress_page_view* view, const operation_labels& labels,
                     const operation_status& status) {
  if (view == nullptr) return;

  // The page subtitle carries the wording; the bar itself stays wordless.
  if (view->subtitle != nullptr) {
    const char* text = labels.running;
    if (status.state == operation_state::DONE)
      text = labels.done;
    else if (status.state == operation_state::FAILED)
      text = labels.failed;
    if (text != nullptr) lv_label_set_text(view->subtitle, text);
  }

  if (view->bar == nullptr || view->bar_fill == nullptr || view->track_width <= 0) return;

  if (status.state == operation_state::RUNNING && status.total <= 0) {
    start_running_animation(view);
    lv_obj_remove_flag(view->bar_fill, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(view->bar_fill, LV_OBJ_FLAG_HIDDEN);

  stop_running_animation(view);
  const int width = view->track_width;
  if (width <= 0) return;
  if (status.state == operation_state::FAILED) {
    lv_obj_set_style_bg_color(view->bar_fill, lv_color_hex(kFailed), LV_PART_MAIN);
    lv_obj_set_style_border_color(view->bar, lv_color_hex(kFailed), LV_PART_MAIN);
    animate_width(view->bar_fill, width);
    return;
  }
  if (status.state == operation_state::DONE) {
    lv_obj_set_style_bg_color(view->bar_fill, lv_color_hex(kDone), LV_PART_MAIN);
    lv_obj_set_style_border_color(view->bar, lv_color_hex(kDone), LV_PART_MAIN);
    animate_width(view->bar_fill, width);
    return;
  }

  const int total = std::max(1, status.total);
  const int done = std::clamp(status.done, 0, total);
  animate_width(view->bar_fill, std::max(1, width * done / total));
}

}  // namespace gui2_pages
