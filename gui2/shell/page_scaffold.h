#ifndef GUI2_SHELL_PAGE_SCAFFOLD_H
#define GUI2_SHELL_PAGE_SCAFFOLD_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

struct page_scaffold_result {
  lv_obj_t* heading = nullptr;
  lv_obj_t* summary = nullptr;
  lv_obj_t* content = nullptr;
};

// Creates the common page heading and the only page-owned scroll viewport.
// Persistent shell overlays are deliberately outside this function.
page_scaffold_result build_page_scaffold(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics,
                                         const char* title, const char* summary,
                                         int bottom_reserved = 0);

}  // namespace gui2_shell

#endif
