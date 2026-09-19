#ifndef GUI2_PAGES_BACKUP_PAGE_H
#define GUI2_PAGES_BACKUP_PAGE_H

#include <cstddef>

#include "backend/backup_backend.h"
#include "components/swipe_slider.h"
#include "components/tab_bar.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct backup_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* page_layer = nullptr;
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;

  const gui2_backend::backup_target* targets = nullptr;
  size_t target_count = 0;
  const bool* selected = nullptr;
  const int* target_indices = nullptr;
  lv_event_cb_t selection_callback = nullptr;

  bool compress = false;
  bool skip_digest = false;
  bool encrypt = false;
  const int* compress_target = nullptr;
  const int* skip_digest_target = nullptr;
  const int* encrypt_target = nullptr;
  lv_event_cb_t option_callback = nullptr;
  lv_event_cb_t keyboard_event_callback = nullptr;

  gui2_components::tab_bar* tabs = nullptr;
  gui2_components::tab_bar::change_callback tab_callback = nullptr;
  void* tab_user_data = nullptr;
  size_t active_tab = 0;

  gui2_components::swipe_slider* confirm = nullptr;
  void (*confirm_callback)(void*) = nullptr;
  void* confirm_user_data = nullptr;
};

struct backup_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* partitions_pane = nullptr;
  lv_obj_t* options_pane = nullptr;
  lv_obj_t* name_input = nullptr;
  lv_obj_t* password_block = nullptr;
  lv_obj_t* password_input = nullptr;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* slider_track = nullptr;
};

backup_page_view build_backup_page(const backup_page_options& options);

// Shows which pane the freshly selected tab owns.
void show_backup_tab(const backup_page_view& view, size_t index);

// The password only matters once the backup is set to be encrypted.
void show_backup_password(const backup_page_view& view, bool visible);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_BACKUP_PAGE_H
