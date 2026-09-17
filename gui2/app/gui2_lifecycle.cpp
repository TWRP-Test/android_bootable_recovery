#include "app/gui2_lifecycle.h"

#include "core/ui_metrics.h"
#include "gui2_display.h"
#include "gui2_input.h"
#include "gui2_svg_cache.h"
#include "twrpminui/minui.h"

namespace gui2_app {

bool initialize_graphics(const gui2_context* context, graphics_state* state,
                         uint32_t (*tick_callback)(void)) {
  if (context == nullptr || state == nullptr || tick_callback == nullptr) return false;
  state->should_exit_display = context->display_initialized;
  if (!context->display_initialized) {
    if (gr_init() < 0) return false;
    state->should_exit_display = true;
  }

  ev_init();
  state->events_initialized = true;
  lv_init();
  state->lv_initialized = true;
  lv_tick_set_cb(tick_callback);

  gui2_core::ui.scale = gui2_core::ui_scale_for(gr_fb_width(), gr_fb_height());
  if (!state->fonts.initialize(gui2_core::ui.scale)) {
    shutdown_graphics(state, false);
    return false;
  }
  state->text_font = state->fonts.text();
  state->status_font = state->fonts.status();
  state->brand_font = state->fonts.brand();
  for (int i = 0; i < gui2_theme::font_manager::console_count(); ++i)
    state->console_fonts[i] = state->fonts.console(i);

  if (gui2_display_init() == nullptr) {
    shutdown_graphics(state, false);
    return false;
  }
  state->pointer_indev = gui2_input_init();
  if (state->pointer_indev == nullptr) {
    shutdown_graphics(state, false);
    return false;
  }
  lv_timer_set_period(lv_indev_get_read_timer(state->pointer_indev), 5);
#if LV_USE_GESTURE_RECOGNITION
  lv_indev_set_pinch_up_threshold(state->pointer_indev, 1.20f);
  lv_indev_set_pinch_down_threshold(state->pointer_indev, 0.80f);
#endif
  return true;
}

void shutdown_graphics(graphics_state* state, bool keep_display) {
  if (state == nullptr) return;
  if (state->lv_initialized) {
    lv_deinit();
    state->lv_initialized = false;
  }
  gui2_svg_cache_clear();
  gui2_display_deinit();
  state->fonts.shutdown();
  state->text_font = nullptr;
  state->status_font = nullptr;
  state->brand_font = nullptr;
  for (lv_font_t*& font : state->console_fonts) font = nullptr;
  state->pointer_indev = nullptr;
  if (state->events_initialized) {
    ev_exit();
    state->events_initialized = false;
  }
  if (!keep_display && state->should_exit_display) {
    gr_exit();
    state->should_exit_display = false;
  }
}

}  // namespace gui2_app
