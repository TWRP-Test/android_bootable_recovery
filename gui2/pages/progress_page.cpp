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

static lv_obj_t* create_action_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                      const progress_action& action, bool primary,
                                      int width, int height, lv_event_cb_t press_guard) {
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_clickable(button, true);
  lv_obj_set_style_radius(button, height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  const lv_color_t face = primary ? lv_color_hex(kAccent) : metrics.card_color;
  gui2_core::set_surface_style(button, face);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), face, 18),
                            LV_STATE_PRESSED);
  gui2_core::disable_scrolling(button);
  if (press_guard != nullptr) lv_obj_add_event_cb(button, press_guard, LV_EVENT_ALL, nullptr);
  if (action.callback != nullptr)
    lv_obj_add_event_cb(button, action.callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, action.label == nullptr ? "" : action.label);
  lv_obj_set_style_text_color(label, primary ? lv_color_hex(0xFFFFFF) : metrics.primary_text,
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

// The row only exists when both labels are set; everything below the content
// has to agree on that, so ask once.
static bool has_actions(const progress_page_options& options) {
  return options.page_layer != nullptr && options.metrics != nullptr &&
         options.left_action.label != nullptr && options.right_action.label != nullptr;
}

static bool has_running_action(const progress_page_options& options) {
  return options.page_layer != nullptr && options.metrics != nullptr &&
         options.running_action.label != nullptr;
}

static int bottom_reserved_for(const progress_page_options& options) {
  return has_actions(options) || has_running_action(options)
             ? progress_actions_height(*options.metrics)
             : gui2_core::navigation_safe_area();
}

static void build_actions(progress_page_view* view,
                          const progress_page_options& options) {
  if (has_running_action(options)) {
    const auto& metrics = *options.metrics;
    const int height = gui2_core::single_line_card_height();
    view->running_action =
        create_action_button(options.page_layer, metrics, options.running_action, false,
                             metrics.content_width, height, options.press_guard_callback);
    lv_obj_set_pos(view->running_action, metrics.outer_margin,
                   metrics.height - metrics.status_height - metrics.outer_margin - height);
  }
  if (!has_actions(options)) return;

  const auto& metrics = *options.metrics;
  const int height = gui2_core::single_line_card_height();
  const int gap = metrics.card_gap;
  const int width = (metrics.content_width - gap) / 2;

  view->actions = lv_obj_create(options.page_layer);
  lv_obj_set_size(view->actions, metrics.content_width, height);
  lv_obj_set_pos(view->actions, metrics.outer_margin,
                 metrics.height - metrics.status_height - metrics.outer_margin - height);
  gui2_core::set_surface_style(view->actions, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view->actions, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(view->actions, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view->actions);
  lv_obj_set_hidden(view->actions, true);

  lv_obj_t* left = create_action_button(view->actions, metrics, options.left_action, false, width,
                                        height, options.press_guard_callback);
  lv_obj_set_pos(left, 0, 0);
  lv_obj_t* right = create_action_button(view->actions, metrics, options.right_action, true,
                                         metrics.content_width - width - gap, height,
                                         options.press_guard_callback);
  lv_obj_set_pos(right, width + gap, 0);
}

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
    // The console page sizes itself against the whole viewport, which knows
    // nothing about what this page reserved at the bottom. Redo the scaffold's
    // own arithmetic so the bar lands exactly on the bottom of the content box.
    const int scroll_top = metrics.heading_top + metrics.heading_height + metrics.cards_top_gap;
    const int available =
        metrics.height - metrics.status_height - scroll_top - bottom_reserved_for(options);
    const int height =
        std::max(gui2_core::ui_px(300), available - bar_height - metrics.cards_top_gap);
    lv_obj_set_pos(view.console.body, 0, 0);
    lv_obj_set_height(view.console.body, height);
    view.console.minimum_height = height;
  }

  view.bar = lv_obj_create(view.body);
  lv_obj_set_size(view.bar, metrics.content_width, bar_height);
  view.track_color = lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 38);
  gui2_core::set_surface_style(view.bar, view.track_color);
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
  lv_obj_set_hidden(view.bar_fill, true);
  lv_obj_set_pos(view.bar_fill, 0, 0);
  gui2_core::set_surface_style(view.bar_fill, lv_color_hex(kAccent));
  lv_obj_set_style_radius(view.bar_fill, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar_fill, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar_fill);
  build_actions(&view, options);
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

  // Whatever happens to the bar, the user decides when to leave.
  if (view->actions != nullptr) {
    if (status.state == operation_state::RUNNING)
      lv_obj_set_hidden(view->actions, true);
    else
      lv_obj_set_hidden(view->actions, false);
  }
  if (view->running_action != nullptr && status.state != operation_state::RUNNING)
    lv_obj_set_hidden(view->running_action, true);

  if (view->bar == nullptr || view->bar_fill == nullptr || view->track_width <= 0) return;

  if (status.state == operation_state::RUNNING) {
    lv_obj_set_style_bg_color(view->bar, view->track_color, LV_PART_MAIN);
    lv_obj_set_style_border_color(view->bar, lv_color_hex(kAccent), LV_PART_MAIN);
  }

  if (status.state == operation_state::RUNNING && status.total <= 0) {
    start_running_animation(view);
    lv_obj_set_hidden(view->bar_fill, false);
    return;
  }
  lv_obj_set_hidden(view->bar_fill, false);

  stop_running_animation(view);
  const int width = view->track_width;
  if (width <= 0) return;
  // A finished bar is one solid rounded rect and nothing else. Leaving the
  // fill on top of it puts a second rounded edge inside the first, and the two
  // antialiased arcs show up as a hairline along both caps.
  if (status.state != operation_state::RUNNING) {
    const lv_color_t color =
        lv_color_hex(status.state == operation_state::FAILED ? kFailed : kDone);
    lv_obj_set_style_bg_color(view->bar, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(view->bar_fill, color, LV_PART_MAIN);
    lv_obj_set_style_border_color(view->bar, color, LV_PART_MAIN);
    lv_obj_set_width(view->bar_fill, width);
    lv_obj_set_hidden(view->bar_fill, true);
    return;
  }

  const int total = std::max(1, status.total);
  const int done = std::clamp(status.done, 0, total);
  animate_width(view->bar_fill, std::max(1, width * done / total));
}

}  // namespace gui2_pages

int gui2_pages::progress_actions_height(const gui2_core::ui_metrics& metrics) {
  // Gap above matches the one inside the page; the inset below matches the
  // one on either side.
  return metrics.cards_top_gap + gui2_core::single_line_card_height() + metrics.outer_margin;
}
