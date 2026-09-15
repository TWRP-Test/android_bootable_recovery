#ifndef GUI2_SHELL_PAGE_HOST_H
#define GUI2_SHELL_PAGE_HOST_H

#include "core/ui_metrics.h"
#include "lvgl.h"
#include "shell/page_scaffold.h"

namespace gui2_shell {

// Owns the page layer and the current page's scaffold objects. Persistent
// status/navigation/quick-panel overlays are intentionally outside this host.
class page_host {
 public:
  void initialize(lv_obj_t* layer, const gui2_core::ui_metrics& metrics);
  page_scaffold_result build(const char* title, const char* summary, int bottom_reserved = 0);
  void clear();

  lv_obj_t* layer() const {
    return layer_;
  }
  lv_obj_t* content() const {
    return content_;
  }

 private:
  lv_obj_t* layer_ = nullptr;
  const gui2_core::ui_metrics* metrics_ = nullptr;
  lv_obj_t* content_ = nullptr;
};

}  // namespace gui2_shell

#endif
