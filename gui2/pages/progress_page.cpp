#include "pages/progress_page.h"

#include <algorithm>
#include <cstdio>

#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

// A single blue comet travels forward, with a longer fading tail.
void sweep_cb(void* object, int32_t phase) {
  auto* comet = static_cast<lv_obj_t*>(object);
  const int width = lv_obj_get_content_width(lv_obj_get_parent(comet));
  const int tail = std::max(1, width * 34 / 100);
  lv_obj_set_width(comet, tail);
  lv_obj_set_x(comet, -tail + (width + tail) * phase / 1000);
}

void start_running_animation(progress_page_view* view) {
  if (view->sweeping || view->bar_tail == nullptr) return;
  lv_obj_set_hidden(view->bar_tail, false);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, view->bar_tail);
  lv_anim_set_exec_cb(&animation, sweep_cb);
  lv_anim_set_values(&animation, 0, 1000);
  lv_anim_set_duration(&animation, 1800);
  lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&animation);
  view->sweeping = true;
}

void stop_running_animation(progress_page_view* view) {
  if (!view->sweeping) return;
  lv_anim_delete(view->bar_tail, sweep_cb);
  lv_obj_set_hidden(view->bar_tail, true);
  view->sweeping = false;
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
  const lv_color_t face = primary ? metrics.accent : metrics.card_color;
  gui2_core::set_surface_style(button, face);
  lv_obj_set_style_bg_color(button, gui2_core::tinted(face, 18), LV_STATE_PRESSED);
  gui2_core::disable_scrolling(button);
  if (press_guard != nullptr) lv_obj_add_event_cb(button, press_guard, LV_EVENT_ALL, nullptr);
  if (action.callback != nullptr)
    lv_obj_add_event_cb(button, action.callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, action.label == nullptr ? "" : action.label);
  lv_obj_set_style_text_color(label, primary ? metrics.on_accent : metrics.primary_text,
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
    lv_obj_set_pos(view->running_action, metrics.content_left,
                   metrics.height - metrics.status_height - metrics.outer_margin - height);
  }
  if (!has_actions(options)) return;

  const auto& metrics = *options.metrics;
  const int height = gui2_core::single_line_card_height();
  const int gap = metrics.card_gap;
  const int width = (metrics.content_width - gap) / 2;

  view->actions = lv_obj_create(options.page_layer);
  lv_obj_set_size(view->actions, metrics.content_width, height);
  lv_obj_set_pos(view->actions, metrics.content_left,
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
  view->right_button = right;
  view->right_label = lv_obj_get_child(right, 0);
}

progress_page_view build_progress_page(const progress_page_options& options) {
  progress_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  view.subtitle = options.subtitle;
  view.strings = options.strings;
  view.task_layout = options.task_layout;
  view.right_action = options.right_action;
  view.failed_right_action = options.failed_right_action;
  const int bar_height = options.task_layout ? gui2_core::ui_px(20) :
      std::clamp(gui2_core::ui_px(150), gui2_core::ui_px(100), gui2_core::ui_px(180));
  const int border = options.task_layout ? 0 : std::max(2, gui2_core::ui_px(4));

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.content_left, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.cards_top_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.body);

  if (options.task_layout) {
    // The log and queue own their scrolling; the scaffold needs no scroll reserve.
    lv_obj_set_style_pad_bottom(options.content, 0, 0);
    gui2_core::disable_scrolling(options.content);
    view.task = lv_obj_create(view.body);
    gui2_core::set_surface_style(view.task, metrics.card_color);
    lv_obj_set_width(view.task, metrics.content_width);
    lv_obj_set_height(view.task, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(view.task, gui2_core::ui_px(48), 0);
    lv_obj_set_style_pad_all(view.task, gui2_core::ui_px(44), 0);
    lv_obj_set_style_pad_row(view.task, gui2_core::ui_px(10), 0);
    lv_obj_set_layout(view.task, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view.task, LV_FLEX_FLOW_COLUMN);
    gui2_core::disable_scrolling(view.task);
    const int text_width = metrics.content_width - gui2_core::ui_px(88);
    auto label = [&](const lv_font_t* font, lv_color_t color) {
      auto* object = lv_label_create(view.task);
      lv_label_set_text(object, "");
      lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(object, text_width);
      lv_obj_set_style_text_font(object, font, 0);
      lv_obj_set_style_text_color(object, color, 0);
      return object;
    };
    view.filename = label(metrics.text_font, metrics.primary_text);
    view.folder = label(metrics.status_font, metrics.secondary_text);
    view.queue_toggle = lv_obj_create(view.task);
    gui2_core::set_surface_style(view.queue_toggle, metrics.card_color, LV_OPA_TRANSP);
    lv_obj_set_size(view.queue_toggle, text_width, gui2_core::ui_px(76));
    lv_obj_set_style_pad_all(view.queue_toggle, 0, 0);
    lv_obj_set_clickable(view.queue_toggle, true);
    gui2_core::disable_scrolling(view.queue_toggle);
    view.queue_label = lv_label_create(view.queue_toggle);
    lv_obj_set_style_text_font(view.queue_label, metrics.status_font, 0);
    lv_obj_set_style_text_color(view.queue_label, metrics.secondary_text, 0);
    lv_obj_center(view.queue_label);
    view.queue_body = lv_obj_create(view.task);
    gui2_core::set_surface_style(view.queue_body, metrics.card_color, LV_OPA_TRANSP);
    lv_obj_set_width(view.queue_body, text_width);
    lv_obj_set_height(view.queue_body, gui2_core::ui_px(300));
    lv_obj_set_style_pad_all(view.queue_body, 0, 0);
    lv_obj_set_style_pad_row(view.queue_body, gui2_core::ui_px(14), 0);
    lv_obj_set_layout(view.queue_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view.queue_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(view.queue_body, LV_DIR_VER);
    gui2_core::style_scrollbar(view.queue_body, metrics.secondary_text);
    lv_obj_set_hidden(view.queue_body, true);
    lv_obj_set_hidden(view.queue_toggle, true);
    view.log_title = lv_label_create(view.body);
    lv_label_set_text(view.log_title, options.log_title == nullptr ? "" : options.log_title);
    lv_obj_set_style_text_font(view.log_title, metrics.status_font, 0);
    lv_obj_set_style_text_color(view.log_title, metrics.control_text, 0);
    if (options.subtitle != nullptr) {
      auto* heading = lv_obj_get_parent(options.subtitle);
      view.elapsed = lv_label_create(heading);
      lv_obj_set_style_text_font(view.elapsed, metrics.status_font, 0);
      lv_obj_set_style_text_color(view.elapsed, metrics.secondary_text, 0);
      lv_obj_align(view.elapsed, LV_ALIGN_BOTTOM_RIGHT, -gui2_core::ui_px(6),
                    -gui2_core::ui_px(6));
      lv_label_set_text(view.elapsed, "");
    }
  }

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

  view.bar = lv_obj_create(options.task_layout ? view.task : view.body);
  lv_obj_set_size(view.bar, options.task_layout ? metrics.content_width - gui2_core::ui_px(88) : metrics.content_width, bar_height);
  view.track_color = gui2_core::tinted(metrics.card_color, 38);
  gui2_core::set_surface_style(view.bar, view.track_color);
  lv_obj_set_style_radius(view.bar, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.bar, border, LV_PART_MAIN);
  lv_obj_set_style_border_color(view.bar, metrics.accent, LV_PART_MAIN);
  lv_obj_set_style_border_opa(view.bar, LV_OPA_COVER, LV_PART_MAIN);
  // Without this the square-ended fill draws past the rounded cap.
  lv_obj_set_style_clip_corner(view.bar, true, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar);

  view.track_width = std::max(0, metrics.content_width - (options.task_layout ? gui2_core::ui_px(88) : 0) - border * 2);
  view.bar_fill = lv_obj_create(view.bar);
  lv_obj_set_size(view.bar_fill, 1, bar_height - border * 2);
  // A one pixel fill pokes out of the rounded end; stay hidden until there is
  // something real to draw.
  lv_obj_set_hidden(view.bar_fill, true);
  lv_obj_set_pos(view.bar_fill, 0, 0);
  gui2_core::set_surface_style(view.bar_fill, metrics.accent);
  lv_obj_set_style_radius(view.bar_fill, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar_fill, 0, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(view.bar_fill, true, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar_fill);

  view.bar_tail = lv_obj_create(view.bar);
  lv_obj_set_size(view.bar_tail, 1, bar_height - border * 2);
  const auto comet_color = gui2_core::tinted(metrics.accent, 28);
  gui2_core::set_surface_style(view.bar_tail, comet_color);
  lv_obj_set_style_radius(view.bar_tail, bar_height / 2, 0);
  lv_obj_set_style_pad_all(view.bar_tail, 0, 0);
  lv_obj_set_style_bg_main_opa(view.bar_tail, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_grad_color(view.bar_tail, comet_color, 0);
  lv_obj_set_style_bg_grad_opa(view.bar_tail, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(view.bar_tail, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_grad_stop(view.bar_tail, 160, 0);
  lv_obj_set_style_clip_corner(view.bar_tail, true, 0);
  gui2_core::disable_scrolling(view.bar_tail);
  lv_obj_set_clickable(view.bar_tail, false);
  lv_obj_set_hidden(view.bar_tail, true);

  auto* head = lv_obj_create(view.bar_tail);
  lv_obj_set_size(head, LV_PCT(12), LV_PCT(100));
  lv_obj_align(head, LV_ALIGN_RIGHT_MID, 0, 0);
  gui2_core::set_surface_style(head, comet_color);
  lv_obj_set_style_radius(head, 0, 0);
  lv_obj_set_style_bg_grad_color(head, gui2_core::tinted(metrics.accent, 160), 0);
  lv_obj_set_style_bg_grad_dir(head, LV_GRAD_DIR_HOR, 0);
  gui2_core::disable_scrolling(head);
  lv_obj_set_clickable(head, false);

  if (options.task_layout) {
    lv_obj_set_style_pad_row(view.body, gui2_core::ui_px(24), 0);
    lv_obj_update_layout(options.content);
    view.available_height = lv_obj_get_content_height(options.content) - metrics.outer_margin;
    view.bottom_reserved = progress_actions_height(metrics);
  }
  build_actions(&view, options);
  return view;
}

namespace {

void layout_task(progress_page_view* view) {
  lv_obj_update_layout(view->task);
  const int reserved = view->bottom_reserved;
  const int top = lv_obj_get_height(view->task) + lv_obj_get_height(view->log_title) +
                  gui2_core::ui_px(48);
  const int height = std::max(gui2_core::ui_px(240), view->available_height - top - reserved);
  lv_obj_set_height(view->console.body, height);
  view->console.minimum_height = height;
}

void toggle_queue(lv_event_t* event) {
  auto* view = static_cast<progress_page_view*>(lv_event_get_user_data(event));
  if (view == nullptr) return;
  view->queue_open = !view->queue_open;
  lv_obj_set_hidden(view->queue_body, !view->queue_open);
  layout_task(view);
}

void update_task(progress_page_view* view, const operation_labels& labels,
                 const operation_status& status) {
  if (!view->task_layout || view->strings == nullptr) return;
  const auto& strings = *view->strings;
  const bool running = status.state == operation_state::RUNNING;
  const bool failed = status.state == operation_state::FAILED;
  const int count = static_cast<int>(status.items.size());
  const int current = std::clamp(status.current, 0, std::max(0, count - 1));
  if (view->elapsed != nullptr) {
    char time[32], text[96];
    const auto seconds = static_cast<unsigned long long>(status.elapsed_ms / 1000);
    std::snprintf(time, sizeof(time), "%02llu:%02llu", seconds / 60, seconds % 60);
    std::snprintf(text, sizeof(text), strings.progress_elapsed, time);
    if (std::string(lv_label_get_text(view->elapsed)) != text)
      lv_label_set_text(view->elapsed, text);
  }
  std::string snapshot = std::to_string(static_cast<int>(status.state)) + ":" +
      std::to_string(current) + ":" + std::to_string(status.completed);
  for (const auto& item : status.items) snapshot += "\n" + item;
  if (snapshot == view->task_snapshot) return;
  view->task_snapshot = snapshot;
  const std::string path = count == 0 ? "" : status.items[current];
  const auto slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const std::string folder = slash == std::string::npos ? "" : path.substr(0, slash);
  lv_label_set_text(view->filename, name.c_str());
  lv_label_set_text(view->folder, folder.c_str());
  lv_obj_set_hidden(view->folder, folder.empty());
  lv_obj_set_hidden(view->queue_toggle, count <= 1);
  if (count > 1) {
    char text[128];
    if (running || failed)
      std::snprintf(text, sizeof(text), strings.progress_item_count, current + 1, count);
    else
      std::snprintf(text, sizeof(text), strings.progress_done_count, status.completed, count);
    lv_label_set_text(view->queue_label, text);
    lv_obj_align(view->queue_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_event_cb(view->queue_toggle, toggle_queue);
    lv_obj_add_event_cb(view->queue_toggle, toggle_queue, LV_EVENT_CLICKED, view);
    lv_obj_clean(view->queue_body);
    for (int i = 0; i < count; ++i) {
      const auto at = status.items[i].find_last_of('/');
      const auto file = at == std::string::npos ? status.items[i] : status.items[i].substr(at + 1);
      const bool done = i < status.completed;
      const bool active = i == current && !done;
      const char* state = done ? strings.progress_item_done :
          active ? (failed ? labels.failed : labels.running) :
          running ? strings.progress_item_waiting : strings.progress_item_skipped;
      auto* row = lv_label_create(view->queue_body);
      lv_label_set_text_fmt(row, "%s · %s", file.c_str(), state == nullptr ? "" : state);
      lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(row, std::max(1, gui2_core::ui.content_width - gui2_core::ui_px(88)));
      lv_obj_set_style_text_font(row, gui2_core::ui.status_font, 0);
      lv_obj_set_style_text_color(row, done ? gui2_core::ui.success :
          active && failed ? gui2_core::ui.danger : active ? gui2_core::ui.primary_text :
          gui2_core::ui.secondary_text, 0);
    }
    if (!running) view->queue_open = true;
    lv_obj_set_hidden(view->queue_body, !view->queue_open);
  } else {
    lv_obj_set_hidden(view->queue_body, true);
  }
  lv_obj_set_hidden(view->bar, !running);
  layout_task(view);
}

} // namespace

void update_progress(progress_page_view* view, const operation_labels& labels,
                     const operation_status& status) {
  if (view == nullptr) return;

  update_task(view, labels, status);
  const bool failed_action = status.state == operation_state::FAILED &&
                             view->failed_right_action.callback != nullptr;
  if (view->right_button != nullptr && failed_action != view->failed_action) {
    const auto& previous = view->failed_action ? view->failed_right_action : view->right_action;
    const auto& next = failed_action ? view->failed_right_action : view->right_action;
    if (previous.callback != nullptr)
      lv_obj_remove_event_cb(view->right_button, previous.callback);
    if (next.callback != nullptr)
      lv_obj_add_event_cb(view->right_button, next.callback, LV_EVENT_CLICKED, nullptr);
    lv_label_set_text(view->right_label, next.label == nullptr ? "" : next.label);
    lv_obj_center(view->right_label);
    const auto color = failed_action ? gui2_core::ui.card_color : gui2_core::ui.accent;
    gui2_core::set_surface_style(view->right_button, color);
    lv_obj_set_style_text_color(view->right_label, failed_action ? gui2_core::ui.primary_text :
                                gui2_core::ui.on_accent, 0);
    view->failed_action = failed_action;
  }

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
    lv_obj_set_hidden(view->bar, false);
    if (view->subtitle != nullptr)
      lv_obj_set_style_text_color(view->subtitle, gui2_core::ui.primary_text, LV_PART_MAIN);
    lv_obj_set_style_bg_color(view->bar, view->track_color, LV_PART_MAIN);
    lv_obj_set_style_border_color(view->bar, gui2_core::ui.accent, LV_PART_MAIN);
  }

  if (status.state == operation_state::RUNNING && status.total <= 0) {
    if (lv_obj_get_parent(view->bar_tail) != view->bar)
      lv_obj_set_parent(view->bar_tail, view->bar);
    lv_obj_set_hidden(view->bar_fill, true);
    start_running_animation(view);
    return;
  }

  const int width = view->track_width;
  if (width <= 0) return;
  // A finished bar is one solid rounded rect and nothing else. Leaving the
  // fill on top of it puts a second rounded edge inside the first, and the two
  // antialiased arcs show up as a hairline along both caps.
  if (status.state != operation_state::RUNNING) {
    stop_running_animation(view);
    const lv_color_t color =
        status.state == operation_state::FAILED ? gui2_core::ui.danger : gui2_core::ui.success;
    lv_obj_set_hidden(view->bar, true);
    if (view->subtitle != nullptr)
      lv_obj_set_style_text_color(view->subtitle, color, LV_PART_MAIN);
    lv_obj_set_hidden(view->bar_fill, true);
    return;
  }

  const int total = std::max(1, status.total);
  const int done = std::clamp(status.done, 0, total);
  lv_obj_set_width(view->bar_fill, std::max(1, static_cast<int>(
      static_cast<int64_t>(width) * done / total)));
  lv_obj_set_hidden(view->bar_fill, done == 0);
  if (done == 0) {
    stop_running_animation(view);
    return;
  }
  // Clip the comet to the reported progress; it must never imply more work
  // has completed than the installer actually reported.
  if (lv_obj_get_parent(view->bar_tail) != view->bar_fill)
    lv_obj_set_parent(view->bar_tail, view->bar_fill);
  start_running_animation(view);
}

}  // namespace gui2_pages

int gui2_pages::progress_actions_height(const gui2_core::ui_metrics& metrics) {
  // Gap above matches the one inside the page; the inset below matches the
  // one on either side.
  return metrics.cards_top_gap + gui2_core::single_line_card_height() + metrics.outer_margin;
}
