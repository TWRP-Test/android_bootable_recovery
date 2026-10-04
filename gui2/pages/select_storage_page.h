#ifndef GUI2_PAGES_SELECT_STORAGE_PAGE_H
#define GUI2_PAGES_SELECT_STORAGE_PAGE_H

#include <cstddef>

#include "backend/mount_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct select_storage_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const gui2_backend::storage_device* storages = nullptr;
  size_t storage_count = 0;
  const int* storage_indices = nullptr;
  lv_event_cb_t select_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

void build_select_storage_page(const select_storage_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_SELECT_STORAGE_PAGE_H
