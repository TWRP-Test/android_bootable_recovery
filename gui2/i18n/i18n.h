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
  const char* brightness_title;
  const char* brightness_summary;
  const char* brightness_label;
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
  const char* timezone_names[24];
  action_text actions[7];
};

const language_pack& get_language_pack(language_id language);

}  // namespace gui2_i18n

#endif  // TWRP_LVGL_DEMO_I18N_H
