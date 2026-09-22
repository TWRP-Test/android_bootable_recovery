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
  // with_fade is off on the progress pages: the fade exists to keep a scrolling
  // list readable behind the navigation, and a running operation has neither.
  page_scaffold_result build(
      const char* title, const char* summary, int bottom_reserved = 0,
      gui2_core::page_transition transition = gui2_core::page_transition::NONE,
      bool with_fade = true);
  void clear();

  // Measures the finished page: spends the reserve as scroll room only when the
  // content would otherwise hide behind whatever floats at the bottom, then
  // decides whether the gradient is needed at all.
  void settle();

  // Starts the slide once the page is built and measured, so the frames
  // are not spent while the content is still being assembled.
  void start_transition();

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
  lv_obj_t* fade_ = nullptr;
  int reserved_ = 0;
  gui2_core::page_transition transition_ = gui2_core::page_transition::NONE;
  bool transition_active_ = false;
  // Set by build(), spent by settle(): the slide waits for the page to
  // have something on it.
  bool transition_pending_ = false;
};

}  // namespace gui2_shell

#endif
