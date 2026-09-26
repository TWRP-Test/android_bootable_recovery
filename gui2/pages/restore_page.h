#ifndef GUI2_PAGES_RESTORE_PAGE_H
#define GUI2_PAGES_RESTORE_PAGE_H

#include <cstddef>

#include "backend/restore_backend.h"
#include "components/keyboard.h"
#include "components/swipe_slider.h"
#include "components/tab_bar.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// Step one: the backups sitting on the current storage.
struct restore_list_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;

  const gui2_backend::restore_backup* backups = nullptr;
  size_t backup_count = 0;
  const int* backup_indices = nullptr;
  lv_event_cb_t select_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

void build_restore_list_page(const restore_list_page_options& options);

// Step two: what to take out of the backup that was picked.
struct restore_page_options {
  lv_obj_t* content = nullptr;
  lv_obj_t* page_layer = nullptr;
  lv_obj_t* overlay_layer = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;

  const gui2_backend::restore_target* targets = nullptr;
  size_t target_count = 0;
  const bool* selected = nullptr;
  const int* target_indices = nullptr;
  lv_event_cb_t selection_callback = nullptr;

  bool check_digest = true;
  const int* check_digest_target = nullptr;
  lv_event_cb_t option_callback = nullptr;
  bool encrypted = false;
  // Shown under the password when the folder refused to open with it.
  bool wrong_password = false;

  gui2_components::tab_bar* tabs = nullptr;
  gui2_components::tab_bar::change_callback tab_callback = nullptr;
  void* tab_user_data = nullptr;
  size_t active_tab = 0;

  gui2_components::keyboard* keyboard = nullptr;
  gui2_components::keyboard_callback key_callback = nullptr;
  void* keyboard_user_data = nullptr;

  gui2_components::swipe_slider* confirm = nullptr;
  void (*confirm_callback)(void*) = nullptr;
  void* confirm_user_data = nullptr;

  // Rename and delete, as on the legacy restore_select page.
  lv_event_cb_t manage_callback = nullptr;
  const void* rename_target = nullptr;
  const void* delete_target = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct restore_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* partitions_pane = nullptr;
  lv_obj_t* options_pane = nullptr;
  lv_obj_t* password_input = nullptr;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* slider_track = nullptr;
};

restore_page_view build_restore_page(const restore_page_options& options);

// Shows which pane the freshly selected tab owns.
void show_restore_tab(const restore_page_view& view, size_t index);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_RESTORE_PAGE_H
