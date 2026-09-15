#ifndef GUI2_SHELL_SCREEN_LOCK_H
#define GUI2_SHELL_SCREEN_LOCK_H

#include "components/swipe_slider.h"
#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

class screen_lock {
 public:
  void create(const gui2_core::ui_metrics& metrics, const char* lock_text, const char* swipe_text,
              void (*unlock_callback)(void*), void* user_data);
  void show();
  void hide();
  bool visible() const;
  void reset();

 private:
  const gui2_core::ui_metrics* metrics_ = nullptr;
  lv_obj_t* root_ = nullptr;
  gui2_components::swipe_slider slider_;
};

}  // namespace gui2_shell

#endif
