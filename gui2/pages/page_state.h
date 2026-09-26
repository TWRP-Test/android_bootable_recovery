#ifndef GUI2_PAGES_PAGE_STATE_H
#define GUI2_PAGES_PAGE_STATE_H

#include <cstddef>
#include <cstdint>

#include "backend/hardware_settings.h"
#include "backend/reboot_backend.h"
#include "components/slider.h"
#include "components/keyboard.h"
#include "components/pattern_lock.h"
#include "components/swipe_slider.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include <string>
#include <vector>

#include "components/tab_bar.h"
#include "pages/console_page.h"
#include "components/keyboard.h"
#include "pages/terminal_page.h"
#include "pages/backup_page.h"
#include "pages/progress_page.h"
#include "pages/restore_page.h"
#include "pages/page_router.h"
#include "pages/reboot_page.h"

namespace gui2_pages {

struct hardware_slider_binding {
  lv_obj_t* value_label = nullptr;
  gui2_backend::haptic_channel channel = gui2_backend::haptic_channel::BUTTON;
  bool brightness = false;
  bool recording_fps = false;
  bool screen_timeout = false;
  bool console_font = false;
  bool keyboard_lift = false;
  gui2_components::slider visual;
};

struct reboot_page_state {
  bool target_selected = false;
  bool has_error = false;
  gui2_backend::reboot_target selected_target = gui2_backend::reboot_target::SYSTEM;
  gui2_pages::page_request return_request{ gui2_pages::page_id::HOME, nullptr,
                                           gui2_core::page_transition::NONE };
  reboot_option options[7] = {};
  gui2_backend::boot_slot slots[2] = { gui2_backend::boot_slot::A, gui2_backend::boot_slot::B };
  gui2_components::swipe_slider confirmation_slider;
};

struct page_state {
  gui2_i18n::language_id current_language = gui2_i18n::language_id::ZH_CN;
  gui2_i18n::language_id pending_language = gui2_i18n::language_id::ZH_CN;
  lv_obj_t* language_option_cards[3] = {};
  lv_obj_t* language_check_labels[3] = {};

  int pending_timezone_index = 0;
  int pending_offset_index = 0;
  bool pending_military_time = false;
  bool pending_dst = false;
  lv_obj_t* timezone_cards[24] = {};
  lv_obj_t* offset_cards[4] = {};
  lv_obj_t* format_cards[2] = {};
  lv_obj_t* dst_card = nullptr;
  lv_obj_t* current_timezone_label = nullptr;

  lv_obj_t* hardware_error_label = nullptr;
  bool hardware_settings_dirty = false;
  hardware_slider_binding brightness_binding;
  hardware_slider_binding screen_timeout_binding;
  hardware_slider_binding console_font_binding;
  hardware_slider_binding keyboard_lift_binding;
  hardware_slider_binding quick_brightness_binding;
  hardware_slider_binding haptic_bindings[3];
  hardware_slider_binding recording_binding;
  bool quick_brightness_dirty = false;
  int screen_timeout_index = 3;
  int console_font_index = 1;
  // The console page has two tabs: recovery output and a shell.
  size_t console_tab = 0;
  gui2_components::tab_bar console_tabs;
  terminal_page_view terminal_view;
  gui2_components::keyboard terminal_keyboard_widget;
  uint64_t terminal_last_poll_ms = 0;
  // How much of the engine's buffer is on screen, and the counter that says
  // whether it changed at all.
  size_t terminal_rendered = 0;
  int terminal_update_counter = -1;
  // The file manager is one page at many depths; this is the depth.
  std::string file_manager_path;
  // What the options page is acting on, and what is waiting to be pasted.
  std::string file_selection;
  bool file_selection_is_folder = false;
  std::string file_clipboard;
  // The rename/chmod field: which of the two, and the mode to prefill.
  bool file_input_is_mode = false;
  std::string file_selection_mode;
  gui2_components::keyboard file_input_keyboard;
  lv_obj_t* file_input = nullptr;
  // Install browses the same way the file manager does, with its own path.
  std::string install_path;
  std::string install_selection;
  // Zips queue up the way legacy queues them; images never do.
  std::vector<std::string> install_queue;
  bool install_image = false;
  size_t install_target_index = 0;
  bool install_both_slots = false;
  gui2_components::swipe_slider install_confirm;
  gui2_components::swipe_slider system_ro_confirm;
  gui2_components::swipe_slider sideload_confirm;
  gui2_components::tab_bar fastbootd_tabs;
  console_page_view fastbootd_console;
  size_t fastbootd_console_consumed = 0;
  uint64_t fastbootd_last_poll_ms = 0;
  bool sideload_wipe_dalvik = false;
  bool sideload_wipe_cache = false;
  progress_page_view sideload_progress;
  size_t sideload_console_consumed = 0;
  uint64_t sideload_last_poll_ms = 0;
  progress_page_view install_progress;
  // Advanced wipe's single partition, and the file system picked for it.
  std::string partition_mount_point;
  int change_fs_selected = -1;
  gui2_components::swipe_slider change_fs_confirm;
  gui2_components::swipe_slider tool_confirm;
  progress_page_view tool_progress;
  size_t tool_console_consumed = 0;
  uint64_t tool_last_poll_ms = 0;
  gui2_components::keyboard tool_input_keyboard;
  lv_obj_t* tool_input = nullptr;
  size_t install_console_consumed = 0;
  uint64_t install_last_poll_ms = 0;
  bool file_clipboard_move = false;
  bool include_kernel_log = false;
  bool include_logcat = true;
  lv_obj_t* kernel_log_card = nullptr;
  lv_obj_t* logcat_card = nullptr;
  lv_obj_t* export_result_label = nullptr;
  console_page_view console;
  progress_page_view wipe_progress;
  lv_obj_t* format_data_input = nullptr;
  lv_obj_t* format_data_track = nullptr;
  gui2_components::swipe_slider format_data_confirm;
  lv_obj_t* format_data_keyboard = nullptr;
  size_t wipe_console_consumed = 0;
  uint64_t wipe_last_poll_ms = 0;
  bool wipe_selected[24] = {};
  size_t wipe_target_count = 0;
  gui2_components::swipe_slider wipe_confirm;
  gui2_components::pattern_lock decrypt_pattern;
  lv_obj_t* decrypt_input = nullptr;
  lv_obj_t* decrypt_keyboard = nullptr;
  gui2_components::keyboard decrypt_keyboard_widget;
  gui2_components::keyboard format_data_keyboard_widget;
  gui2_components::keyboard backup_keyboard_widget;
  lv_obj_t* decrypt_status = nullptr;
  bool decrypt_failed = false;
  bool language_from_decrypt = false;
  progress_page_view decrypt_progress;
  size_t decrypt_console_consumed = 0;
  uint64_t decrypt_last_poll_ms = 0;
  uint64_t progress_settled_ms = 0;
  bool decrypt_refreshing = false;
  gui2_components::tab_bar backup_tabs;
  backup_page_view backup_view;
  gui2_components::swipe_slider backup_confirm;
  bool backup_selected[24] = {};
  size_t backup_target_count = 0;
  size_t backup_active_tab = 0;
  bool backup_compress = true;
  bool backup_skip_digest = false;
  bool backup_encrypt = false;
  progress_page_view backup_progress;
  size_t backup_console_consumed = 0;
  uint64_t backup_last_poll_ms = 0;
  restore_page_view restore_view;
  gui2_components::tab_bar restore_tabs;
  gui2_components::swipe_slider restore_confirm;
  gui2_components::keyboard restore_keyboard_widget;
  bool restore_selected[24] = {};
  size_t restore_target_count = 0;
  size_t restore_active_tab = 0;
  bool restore_check_digest = true;
  bool restore_wrong_password = false;
  std::string restore_path;
  std::string restore_name;
  progress_page_view restore_progress;
  size_t restore_console_consumed = 0;
  uint64_t restore_last_poll_ms = 0;
  size_t mount_target_count = 0;
  size_t console_consumed = 0;
  uint64_t console_last_poll_ms = 0;
  reboot_page_state reboot;
};

}  // namespace gui2_pages

#endif
