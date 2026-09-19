#ifndef TWRP_LVGL_DEMO_I18N_H
#define TWRP_LVGL_DEMO_I18N_H

namespace gui2_i18n {

enum class language_id {
  ENGLISH,
  ZH_CN,
  ZH_TW,
};

struct action_text {
  const char* title;
  const char* summary;
  const char* detail;
};

struct language_pack {
  const char* native_name;
  const char* home_summary;
  const char* language_title;
  const char* language_summary;
  const char* current_language_detail;
  const char* screen_title;
  const char* screen_summary;
  const char* brightness_label;
  const char* screen_timeout_label;
  const char* screen_timeout_never;
  const char* haptics_title;
  const char* haptics_summary;
  const char* button_haptics;
  const char* keyboard_haptics;
  const char* action_haptics;
  const char* hardware_error;
  const char* apply;
  const char* time_title;
  const char* time_summary;
  const char* time_format;
  const char* twelve_hour;
  const char* twenty_four_hour;
  const char* select_timezone;
  const char* timezone_offset;
  const char* offset_none;
  const char* offset_15;
  const char* offset_30;
  const char* offset_45;
  const char* use_dst;
  const char* current_timezone;
  const char* classic_gui_title;
  const char* classic_gui_detail;
  const char* classic_gui_confirm_title;
  const char* classic_gui_confirm_body;
  const char* classic_gui_confirm;
  const char* cancel;
  const char* screenshot;
  const char* screenshot_saved;
  const char* screenshot_failed;
  const char* screen_off;
  const char* screen_off_failed;
  const char* screen_on;
  const char* swipe_to_unlock;
  const char* start_recording;
  const char* stop_recording;
  const char* recording_started;
  const char* recording_saved;
  const char* recording_failed;
  const char* recording_indicator;
  const char* recording_settings_title;
  const char* recording_settings_summary;
  const char* recording_fps_label;
  const char* quick_menu_title;
  const char* reboot_title;
  const char* reboot_summary;
  const char* reboot_system;
  const char* reboot_power_off;
  const char* reboot_recovery;
  const char* reboot_fastboot;
  const char* reboot_bootloader;
  const char* reboot_download;
  const char* reboot_edl;
  const char* current_boot_slot;
  const char* boot_slot_a;
  const char* boot_slot_b;
  const char* swipe_reboot;
  const char* swipe_power_off;
  const char* reboot_failed;
  const char* console_title;
  const char* console_summary;
  const char* console_empty;
  const char* console_settings_title;
  const char* console_settings_summary;
  const char* console_font_label;
  const char* console_font_steps[3];
  const char* export_log_title;
  const char* export_log_summary;
  const char* include_kernel_log;
  const char* include_logcat;
  const char* export_log;
  const char* export_log_done;
  const char* export_log_failed;
  const char* wipe_title;
  const char* wipe_summary;
  const char* factory_reset_detail;
  const char* swipe_factory_reset;
  const char* advanced_wipe_title;
  const char* advanced_wipe_summary;
  const char* select_partitions;
  const char* dalvik_cache;
  const char* swipe_wipe;
  const char* format_data_title;
  const char* format_data_summary;
  const char* format_data_warning;
  const char* format_data_prompt;
  const char* format_data_action;
  const char* swipe_format_data;
  const char* wiping;
  const char* wipe_complete;
  const char* wipe_failed;
  const char* decrypt_title;
  const char* decrypt_summary;
  const char* decrypt_pattern_prompt;
  const char* decrypt_password_prompt;
  const char* decrypt_failed;
  const char* decrypting;
  const char* decrypt_complete;
  const char* backup_title;
  const char* backup_summary;
  const char* backup_partitions_tab;
  const char* backup_options_tab;
  const char* backup_name_label;
  const char* backup_compress;
  const char* backup_skip_digest;
  const char* swipe_backup;
  const char* backing_up;
  const char* backup_complete;
  const char* backup_failed;
  const char* backup_cancelled;
  const char* mount_title;
  const char* mount_summary;
  const char* mount_system_writable;
  const char* mount_system_note;
  const char* refreshing_sizes;
  const char* refresh_sizes_done;
  const char* backup_encrypt;
  const char* backup_password;
  const char* change_language;
  const char* data_encrypted_notice;
  const char* timezone_names[24];
  action_text actions[7];
};

const language_pack& get_language_pack(language_id language);

}  // namespace gui2_i18n

#endif  // TWRP_LVGL_DEMO_I18N_H
