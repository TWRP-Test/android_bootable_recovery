#ifndef GUI2_SHELL_SPLASH_H
#define GUI2_SHELL_SPLASH_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

struct splash_options {
  lv_obj_t* layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const lv_font_t* text_font = nullptr;
  const lv_font_t* small_font = nullptr;
  const char* version = nullptr;
  const char* device = nullptr;
};

struct splash_view {
  lv_obj_t* root = nullptr;
  lv_obj_t* details = nullptr;
  lv_obj_t* status = nullptr;
  lv_obj_t* bar_fill = nullptr;
  int track_width = 0;
  int shown_width = 0;
  bool details_shown = false;
};

// Starts the logo intro; the arrow keeps turning once it is done.
splash_view create_splash(const splash_options& options);
bool splash_intro_finished();

// The status line and bar stay hidden on a normal start and appear only when
// startup runs long. fraction: 0 to 1.
void show_splash_details(splash_view* view);
void update_splash(splash_view* view, const char* text, float fraction);

void dismiss_splash(splash_view* view);

}  // namespace gui2_shell

#endif  // GUI2_SHELL_SPLASH_H
