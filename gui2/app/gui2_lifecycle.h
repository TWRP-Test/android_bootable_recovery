#ifndef GUI2_APP_GUI2_LIFECYCLE_H
#define GUI2_APP_GUI2_LIFECYCLE_H

#include "gui2.h"
#include "lvgl.h"
#include "theme/font_manager.h"

namespace gui2_app {

struct graphics_state {
  gui2_theme::font_manager fonts;
  lv_font_t* text_font = nullptr;
  lv_font_t* status_font = nullptr;
  lv_font_t* brand_font = nullptr;
  lv_font_t* console_fonts[3] = {};
  lv_indev_t* pointer_indev = nullptr;
  bool events_initialized = false;
  bool lv_initialized = false;
  bool should_exit_display = false;
};

bool initialize_graphics(const gui2_context* context, graphics_state* state,
                         uint32_t (*tick_callback)(void));
void shutdown_graphics(graphics_state* state, bool keep_display);

}  // namespace gui2_app

#endif
