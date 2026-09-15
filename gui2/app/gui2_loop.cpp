#include "app/gui2_loop.h"

#include <time.h>
#include <unistd.h>

#include "gui2_display.h"
#include "gui2_input.h"
#include "twrpperf/perf_manager.hpp"

namespace gui2_app {

namespace {

uint64_t monotonic_ms() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

}  // namespace

void run_gui2_loop(gui2_backend::screen_backend* screen, lv_indev_t* pointer_indev, void* user_data,
                   const loop_callbacks& callbacks) {
  if (screen == nullptr || pointer_indev == nullptr) return;
  auto& perf_manager = twrp::TwrpPerfManager::Get();
  perf_manager.Initialize();

  for (;;) {
    if (callbacks.should_exit != nullptr && callbacks.should_exit(user_data)) break;
    const uint64_t loop_start_ms = monotonic_ms();
    if (callbacks.on_tick != nullptr) callbacks.on_tick(user_data, loop_start_ms);
    perf_manager.Update();
    screen->tick(loop_start_ms);
    gui2_input_set_screen_off(screen->is_screen_off());
    uint32_t delay_ms = lv_timer_handler();

    gui2_key_action key_action;
    while (gui2_input_take_key_action(&key_action)) {
      if (callbacks.on_key_action != nullptr) callbacks.on_key_action(user_data, key_action);
    }

    if (gui2_input_take_activity()) {
      const bool was_screen_off = screen->is_screen_off();
      screen->on_input_activity();
      if (was_screen_off) {
        gui2_input_set_screen_off(false);
        lv_obj_invalidate(lv_screen_active());
      }
      if (callbacks.on_activity != nullptr) callbacks.on_activity(user_data, was_screen_off);
      perf_manager.NotifyInteraction();
    }

    const int wheel = gui2_input_take_wheel();
    if (wheel != 0 && callbacks.on_wheel != nullptr) callbacks.on_wheel(user_data, wheel);

    if (gui2_display_present(screen, loop_start_ms)) perf_manager.NotifyFrameActivity();
    if (callbacks.after_present != nullptr) callbacks.after_present(user_data);
    delay_ms = perf_manager.ClampTimeoutMs(delay_ms);

    if (delay_ms == LV_NO_TIMER_READY || delay_ms > 50) delay_ms = 10;
    if (delay_ms == 0) delay_ms = 1;
    const uint64_t elapsed_ms = monotonic_ms() - loop_start_ms;
    if (delay_ms > elapsed_ms) usleep((delay_ms - elapsed_ms) * 1000);
  }
}

}  // namespace gui2_app
