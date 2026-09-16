#ifndef GUI2_SHELL_PAGE_HOST_H
#define GUI2_SHELL_PAGE_HOST_H

#include "core/page_transition.h"
#include "core/ui_metrics.h"
#include "lvgl.h"
#include "shell/page_scaffold.h"

namespace gui2_shell {

// Owns the page layer and the current page's scaffold objects. Persistent
// status/navigation/quick-panel overlays are intentionally outside this host.
class page_host {
 public:
  void initialize(lv_obj_t* layer, const gui2_core::ui_metrics& metrics);
  page_scaffold_result build(
      const char* title, const char* summary, int bottom_reserved = 0,
      gui2_core::page_transition transition = gui2_core::page_transition::NONE);
  void clear();

  lv_obj_t* layer() const {
    return layer_;
  }
  lv_obj_t* content() const {
    return content_;
  }
  lv_obj_t* current_page() const {
    return current_page_;
  }

 private:
  static void animation_exec(void* object, int32_t progress);
  static void animation_ready(lv_anim_t* animation);

  void stop_transition();
  void finish_transition();

  lv_obj_t* layer_ = nullptr;
  const gui2_core::ui_metrics* metrics_ = nullptr;
  lv_obj_t* content_ = nullptr;
  lv_obj_t* current_page_ = nullptr;
  lv_obj_t* previous_page_ = nullptr;
  lv_obj_t* input_blocker_ = nullptr;
  gui2_core::page_transition transition_ = gui2_core::page_transition::NONE;
  bool transition_active_ = false;
};

}  // namespace gui2_shell

#endif
