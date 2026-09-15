#ifndef GUI2_SHELL_QUICK_PANEL_CONTROLLER_H
#define GUI2_SHELL_QUICK_PANEL_CONTROLLER_H

#include "core/ui_metrics.h"
#include "lvgl.h"
#include "shell/quick_panel.h"

namespace gui2_shell {

class quick_panel_controller {
 public:
  void initialize(const gui2_core::ui_metrics& metrics, const quick_panel_view& view,
                  lv_indev_t* pointer_indev, void (*on_open)());

  bool input_active() const;
  bool gesture_tracking() const {
    return gesture_tracking_;
  }
  bool gesture_moved() const {
    return gesture_moved_;
  }
  void stop_tracking() {
    gesture_tracking_ = false;
  }
  void open();
  void close();
  void begin_drag(lv_event_t* event, bool from_dismiss);
  void update_drag(lv_event_t* event);
  void finish_drag();

 private:
  static void animation_exec(void* object, int32_t progress);
  static void animation_ready(lv_anim_t* animation);
  static uint64_t monotonic_ms();

  void set_progress(int progress);
  void animate(bool open);
  void reset_drag();
  lv_indev_t* event_indev(lv_event_t* event) const;

  gui2_core::ui_metrics metrics_;
  quick_panel_view view_;
  lv_indev_t* pointer_indev_ = nullptr;
  void (*on_open_)() = nullptr;
  int progress_ = 0;
  bool animation_target_open_ = false;
  int gesture_start_y_ = 0;
  bool gesture_tracking_ = false;
  bool gesture_moved_ = false;
  int gesture_last_y_ = 0;
  uint64_t gesture_last_ms_ = 0;
  int gesture_velocity_y_ = 0;
  bool gesture_from_dismiss_ = false;
  int gesture_start_progress_ = 0;
};

}  // namespace gui2_shell

#endif
