#ifndef GUI2_PAGES_PROGRESS_PAGE_H
#define GUI2_PAGES_PROGRESS_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/console_page.h"

namespace gui2_pages {

enum class operation_state {
  RUNNING,
  DONE,
  FAILED,
};

// A long running job reported to the user. total <= 0 means the job cannot say
// how far along it is, and the bar animates instead of filling.
struct operation_status {
  operation_state state = operation_state::RUNNING;
  int done = 0;
  int total = 0;
};

// The wording belongs to the caller: only it knows whether this is a wipe, a
// decrypt or an install.
struct operation_labels {
  const char* running = nullptr;
  const char* done = nullptr;
  const char* failed = nullptr;
};

// Legacy TWRP parks the user on the finished page and lets them pick what
// happens next instead of walking away on its own.
struct progress_action {
  const char* label = nullptr;
  lv_event_cb_t callback = nullptr;
};

// What to reserve at the bottom of the page for the pair of buttons.
int progress_actions_height(const gui2_core::ui_metrics& metrics);

struct progress_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const lv_font_t* console_font = nullptr;
  const char* initial_text = nullptr;
  lv_obj_t* subtitle = nullptr;
  // Both actions have to be set for the row to exist; it lives in the page
  // layer so the console keeps its own scrolling.
  lv_obj_t* page_layer = nullptr;
  progress_action left_action;
  progress_action right_action;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct progress_page_view {
  lv_obj_t* body = nullptr;
  // Owned by the page scaffold, not by this page; only the text is ours.
  lv_obj_t* subtitle = nullptr;
  lv_obj_t* bar = nullptr;
  lv_obj_t* bar_fill = nullptr;
  // Measured widths are zero until the first layout pass, so the usable
  // track width is computed up front instead.
  int track_width = 0;
  // Put back when a finished bar starts running again.
  lv_color_t track_color{};
  console_page_view console;
  // Hidden until the job stops running.
  lv_obj_t* actions = nullptr;
  bool sweeping = false;
};

progress_page_view build_progress_page(const progress_page_options& options);

void update_progress(progress_page_view* view, const operation_labels& labels,
                     const operation_status& status);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_PROGRESS_PAGE_H
