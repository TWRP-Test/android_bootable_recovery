#ifndef GUI2_PAGES_INSTALL_CONFIRM_PAGE_H
#define GUI2_PAGES_INSTALL_CONFIRM_PAGE_H

#include <cstddef>

#include "backend/install_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// One checkbox on the confirm page, the way the legacy theme repeats the
// install options there. A null key means the switch only lives for this
// flash instead of being written back to the settings file.
struct install_option {
  const char* label = nullptr;
  const char* key = nullptr;
  bool value = false;
};

// A zip needs confirming, with the options and the queue the legacy
// flash_confirm page carries. An image needs a target partition instead,
// which is the legacy flashimage_confirm page.
struct install_confirm_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  bool image = false;
  // What is about to be flashed, split the way legacy previews it.
  const char* folder = nullptr;
  const char* file = nullptr;
  // The zip queue, oldest first. Images never queue.
  const char* const* queue = nullptr;
  size_t queue_count = 0;
  size_t queue_limit = 0;
  const gui2_backend::image_target* targets = nullptr;
  size_t target_count = 0;
  const int* target_indices = nullptr;
  size_t selected_target = 0;
  lv_event_cb_t target_callback = nullptr;
  const install_option* option_list = nullptr;
  size_t option_count = 0;
  const int* option_indices = nullptr;
  lv_event_cb_t option_callback = nullptr;
  lv_event_cb_t add_zip_callback = nullptr;
  lv_event_cb_t clear_queue_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

void build_install_confirm_page(const install_confirm_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_INSTALL_CONFIRM_PAGE_H
