#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>

#include "backend/hardware_settings.h"
#include "backend/screen_backend.h"
#include "backend/status_backend.h"
#include "components/slider.h"
#include "gui2.h"
#include "gui2_display.h"
#include "gui2_input.h"
#include "gui2_svg_assets.h"
#include "gui2_svg_cache.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include "twrpminui/minui.h"
#include "twrpperf/perf_manager.hpp"

// Load WQY from recovery resources.

static lv_font_t* runtime_text_font;
static lv_font_t* runtime_status_font;
static lv_font_t* runtime_brand_font;
static gui2_backend::settings_store* settings;
static gui2_backend::hardware_settings* hardware;
static gui2_backend::screen_backend* screen;
static std::unique_ptr<gui2_backend::status_backend> status_provider;
static lv_timer_t* status_timer;

struct ui_metrics {
  int width;
  int height;
  float scale;
  int status_height;
  int status_top_padding;
  int nav_height;
  int outer_margin;
  int card_gap;
  int content_width;
  int heading_top;
  int cards_top_gap;
  int heading_height;
  int card_height;
  int icon_size;
  const lv_font_t* text_font;
  const lv_font_t* status_font;
  const lv_font_t* brand_font;
  lv_color_t background;
  lv_color_t card_color;
  lv_color_t nav_color;
  lv_color_t primary_text;
  lv_color_t secondary_text;
};

static ui_metrics ui;

static int ui_px(float value) {
  return std::max(1, static_cast<int>(std::lround(value * ui.scale)));
}

static int navigation_safe_area(void) {
  return ui.nav_height + ui.outer_margin;
}

static const lv_font_t* ui_text_font(void) {
  return runtime_text_font;
}

static float ui_scale_for(int width, int height) {
  constexpr float reference_short_side = 1200.0f;
  return std::clamp(std::min(width, height) / reference_short_side, 0.72f, 2.0f);
}

static void init_ui_font(void) {
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  // WQY is a TTC; TinyTTF uses its first face.
  ui.scale = ui_scale_for(gr_fb_width(), gr_fb_height());
  const int text_size = ui_px(50);
  const int status_size = ui_px(42);
  const int brand_size = ui_px(104);
  runtime_text_font = lv_tiny_ttf_create_file("/twres/fonts/wqy-microhei.ttf", text_size);
  runtime_status_font = lv_tiny_ttf_create_file("/twres/fonts/wqy-microhei.ttf", status_size);
  runtime_brand_font = lv_tiny_ttf_create_file("/twres/fonts/wqy-microhei.ttf", brand_size);
#endif
}

static uint64_t monotonic_ms(void) {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static uint32_t lv_tick_ms(void) {
  return static_cast<uint32_t>(monotonic_ms());
}

static lv_indev_t* pointer_indev;
static lv_obj_t* main_content;
static lv_obj_t* status_hint;
static lv_obj_t* page_layer;
static lv_obj_t* heading;
static lv_obj_t* status_time_label;
static lv_obj_t* battery_value_label;
static lv_obj_t* battery_icon;
static lv_obj_t* recording_indicator;
static lv_obj_t* nav_home_icon;
static lv_obj_t* nav_console_icon;
static lv_obj_t* nav_power_icon;
static lv_obj_t* mouse_cursor;
static lv_obj_t* legacy_dialog;
static lv_obj_t* quick_menu;
static lv_obj_t* quick_dismiss;
static lv_obj_t* quick_record_button;
static lv_obj_t* quick_record_label;
static lv_obj_t* quick_feedback;
static lv_obj_t* screenshot_flash;
static int quick_gesture_start_y;
static bool quick_gesture_tracking;
static bool quick_gesture_moved;
static int quick_gesture_last_y;
static uint64_t quick_gesture_last_ms;
static int quick_gesture_velocity_y;
static bool quick_gesture_from_dismiss;
static int quick_gesture_start_progress;
static int quick_menu_progress;
static int quick_menu_height;
static int quick_menu_closed_y;
static int quick_menu_open_y;
static bool quick_menu_animation_target_open;
static bool pending_screenshot;
static bool pending_screen_off;
static uint64_t screenshot_flash_until_ms;
static float wheel_scroll_velocity;
static float wheel_scroll_remainder;
static uint64_t wheel_scroll_last_ms;
static bool home_page_active;
static bool home_navigation_active;
static lv_obj_t* click_target;
static bool click_cancelled;
static bool switch_to_legacy;

using app_language = gui2_i18n::language_id;
using language_pack = gui2_i18n::language_pack;

enum class page_kind {
  HOME,
  ACTION,
  LANGUAGE,
  TIMEZONE,
  BRIGHTNESS,
  HAPTICS,
  RECORDING,
};

static app_language current_language = app_language::ZH_CN;
static app_language pending_language = app_language::ZH_CN;
static page_kind current_page = page_kind::HOME;

static const language_pack& strings(void) {
  return gui2_i18n::get_language_pack(current_language);
}

static const char* language_name(app_language language) {
  return gui2_i18n::get_language_pack(language).native_name;
}

static const lv_image_dsc_t* battery_icon_for(int percentage, bool charging) {
  const int clamped_percentage = std::clamp(percentage, 0, 100);
  if (clamped_percentage == 0) return charging ? &kGui2IconBattery0Charging : &kGui2IconBattery0;
  const int level = std::clamp(((clamped_percentage + 5) / 10) * 10, 10, 100);
  switch (level) {
    case 10:
      return charging ? &kGui2IconBattery10Charging : &kGui2IconBattery10;
    case 20:
      return charging ? &kGui2IconBattery20Charging : &kGui2IconBattery20;
    case 30:
      return charging ? &kGui2IconBattery30Charging : &kGui2IconBattery30;
    case 40:
      return charging ? &kGui2IconBattery40Charging : &kGui2IconBattery40;
    case 50:
      return charging ? &kGui2IconBattery50Charging : &kGui2IconBattery50;
    case 60:
      return charging ? &kGui2IconBattery60Charging : &kGui2IconBattery60;
    case 70:
      return charging ? &kGui2IconBattery70Charging : &kGui2IconBattery70;
    case 80:
      return charging ? &kGui2IconBattery80Charging : &kGui2IconBattery80;
    case 90:
      return charging ? &kGui2IconBattery90Charging : &kGui2IconBattery90;
    default:
      return charging ? &kGui2IconBattery100Charging : &kGui2IconBattery100;
  }
}

static void scale_icon_font(lv_obj_t* object) {
  if (object == nullptr) return;
  lv_obj_update_layout(object);
  lv_obj_set_style_transform_pivot_x(object, lv_obj_get_width(object) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(object, lv_obj_get_height(object) / 2, LV_PART_MAIN);
  lv_obj_set_style_transform_scale(
      object, static_cast<int32_t>(std::lround(ui.scale * LV_SCALE_NONE)), LV_PART_MAIN);
}

static lv_obj_t* create_svg_image(lv_obj_t* parent, const lv_image_dsc_t* source, int width,
                                  int height) {
  if (parent == nullptr || source == nullptr) return nullptr;
  lv_obj_t* image = lv_image_create(parent);
  if (image == nullptr) return nullptr;
  lv_image_set_src(image, gui2_svg_get_raster(source, width, height));
  lv_obj_set_size(image, std::max(1, width), std::max(1, height));
  lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
  lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
  return image;
}

static int action_icon_art_size(int color_block_size) {
  // The source SVGs use almost the full viewBox. Leave a deliberate inset so
  // the white artwork does not visually touch the colored rounded square.
  return std::max(1, color_block_size * 82 / 100);
}

enum class action_id {
  INSTALL,
  WIPE,
  BACKUP,
  RESTORE,
  MOUNT,
  ADVANCED,
  SETTINGS,
};

struct action_definition {
  action_id id;
  const lv_image_dsc_t* icon;
  uint32_t color;
};

static const action_definition actions[] = {
  { action_id::INSTALL, &kGui2IconInstall, 0x347FF1 },
  { action_id::WIPE, &kGui2IconWipe, 0xF0443E },
  { action_id::BACKUP, &kGui2IconBackup, 0xFFAA20 },
  { action_id::RESTORE, &kGui2IconRestore, 0x18C935 },
  { action_id::MOUNT, &kGui2IconMount, 0x6754E8 },
  { action_id::ADVANCED, &kGui2IconAdvanced, 0x9AA7B0 },
  { action_id::SETTINGS, &kGui2IconSettings, 0x405A6C },
};

static lv_obj_t* language_option_cards[3];
static lv_obj_t* language_check_labels[3];
static constexpr app_language language_values[3] = {
  app_language::ENGLISH,
  app_language::ZH_CN,
  app_language::ZH_TW,
};

static void show_home_page(void);
static void show_action_page(const action_definition& definition);
static void show_language_page(void);
static void show_timezone_page(void);
static void show_brightness_page(void);
static void show_haptics_page(void);
static void show_recording_page(void);
static void create_gui2_shell(lv_obj_t* screen);
static lv_obj_t* create_apply_button(lv_event_cb_t callback, const char* text);
static int card_inner_padding(void);
static void close_quick_menu(void);
static void show_screenshot_flash(void);
static void refresh_recording_ui(void);
static void layout_status_labels(void);
static void refresh_navigation_icons(void);
static void status_gesture_event_cb(lv_event_t* event);
static void quick_panel_gesture_event_cb(lv_event_t* event);
static void create_quick_menu(void);

static constexpr const char* timezone_values[24] = {
  "BST11;BDT",
  "HST10;HDT",
  "AST9;ADT",
  "PST8;PDT,M3.2.0,M11.1.0",
  "MST7;MDT,M3.2.0,M11.1.0",
  "CST6;CDT,M3.2.0,M11.1.0",
  "EST5;EDT,M3.2.0,M11.1.0",
  "AST4;ADT",
  "GRNLNDST3;GRNLNDDT",
  "FALKST2;FALKDT",
  "AZOREST1;AZOREDT",
  "GMT0;BST,M3.5.0,M10.5.0",
  "CET-1;CEST,M3.5.0,M10.5.0",
  "WET-2;WET,M3.2.0,M10.5.0",
  "SAUST-3;SAUDT",
  "WST-4;WDT",
  "PAKST-5;PAKDT",
  "TASHST-6;TASHDT",
  "THAIST-7;THAIDT",
  "TAIST-8;TAIDT",
  "JST-9;JSTDT",
  "EET-10;EETDT",
  "MET-11;METDT",
  "NZST-12;NZDT",
};

static constexpr const char* timezone_offsets[4] = { "0", "15", "30", "45" };
static constexpr int timezone_indices[24] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
};
static constexpr int offset_indices[4] = { 0, 1, 2, 3 };
static constexpr int format_indices[2] = { 0, 1 };
static constexpr int recording_fps_values[5] = { 15, 24, 30, 45, 60 };

static int recording_fps_limit(void) {
  return screen == nullptr ? 60 : std::clamp(screen->max_recording_fps(), 15, 60);
}

static int recording_fps_count(void) {
  int count = 0;
  for (const int fps : recording_fps_values) {
    if (fps <= recording_fps_limit()) ++count;
  }
  return std::max(1, count);
}

static int recording_fps_at(int index) {
  const int count = recording_fps_count();
  index = std::clamp(index, 0, count - 1);
  int available_index = 0;
  for (const int fps : recording_fps_values) {
    if (fps <= recording_fps_limit()) {
      if (available_index++ == index) return fps;
    }
  }
  return recording_fps_values[0];
}

static int pending_timezone_index;
static int pending_offset_index;
static bool pending_military_time;
static bool pending_dst;
static lv_obj_t* timezone_cards[24];
static lv_obj_t* offset_cards[4];
static lv_obj_t* format_cards[2];
static lv_obj_t* dst_card;
static lv_obj_t* current_timezone_label;
static lv_obj_t* hardware_error_label;
static bool hardware_settings_dirty;

struct hardware_slider_binding {
  lv_obj_t* value_label;
  gui2_backend::haptic_channel channel;
  bool brightness;
  bool recording_fps;
  gui2_components::slider visual;
};

static hardware_slider_binding brightness_binding;
static hardware_slider_binding quick_brightness_binding;
static hardware_slider_binding haptic_bindings[3];
static hardware_slider_binding recording_binding;
static bool quick_brightness_dirty;

static void refresh_status_bar(lv_timer_t* timer);
static void hardware_slider_state_event_cb(lv_event_t* event);

static const char* language_code(app_language language) {
  switch (language) {
    case app_language::ENGLISH:
      return "en";
    case app_language::ZH_CN:
      return "zh_CN";
    case app_language::ZH_TW:
      return "zh_TW";
  }
  return "en";
}

static app_language language_from_code(const std::string& code) {
  if (code == "zh_CN") return app_language::ZH_CN;
  if (code == "zh_TW") return app_language::ZH_TW;
  return app_language::ENGLISH;
}

static void set_surface_style(lv_obj_t* object, lv_color_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opa, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
}

static void disable_scrolling(lv_obj_t* object) {
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

static void create_navigation_fade(void) {
  if (page_layer == nullptr) return;

  const int fade_overhang = std::max(ui_px(24), ui.outer_margin / 2);
  const int fade_height = ui.nav_height + fade_overhang;
  const int fade_top = std::max(0, ui.height - ui.status_height - fade_height);
  lv_obj_t* fade = lv_obj_create(page_layer);
  lv_obj_set_pos(fade, 0, fade_top);
  lv_obj_set_size(fade, ui.width, fade_height);
  // Keep the object drawable so LVGL evaluates the gradient.  The first
  // gradient stop is transparent, so this does not add a solid rectangle.
  set_surface_style(fade, lv_color_hex(0x000000), LV_OPA_COVER);
  lv_obj_set_style_radius(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_main_opa(fade, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(fade, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_opa(fade, static_cast<lv_opa_t>(190), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_bg_main_stop(fade, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_stop(fade, 255, LV_PART_MAIN);
  lv_obj_clear_flag(fade, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(fade);
}

static void press_cancel_guard_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));

  if (code == LV_EVENT_PRESSED) {
    click_target = target;
    click_cancelled = false;
  } else if (code == LV_EVENT_PRESSING && click_target == target && pointer_indev != nullptr) {
    // Detect leaving the hit area even without PRESS_LOST.
    lv_point_t point;
    lv_indev_get_point(pointer_indev, &point);
    lv_area_t click_area;
    lv_obj_get_click_area(target, &click_area);
    if (point.x < click_area.x1 || point.x > click_area.x2 || point.y < click_area.y1 ||
        point.y > click_area.y2) {
      click_cancelled = true;
    }
  } else if (code == LV_EVENT_PRESS_LOST && click_target == target) {
    click_cancelled = true;
  }
}

static void add_press_cancel_guard(lv_obj_t* object) {
  lv_obj_add_event_cb(object, press_cancel_guard_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(object, press_cancel_guard_cb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(object, press_cancel_guard_cb, LV_EVENT_PRESS_LOST, nullptr);
}

static bool accept_click(lv_event_t* event) {
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  const bool accepted = click_target == nullptr || (click_target == target && !click_cancelled);
  bool inside = true;
  if (accepted && pointer_indev != nullptr && target != nullptr) {
    lv_point_t point;
    lv_indev_get_point(pointer_indev, &point);
    lv_area_t click_area;
    lv_obj_get_click_area(target, &click_area);
    inside = point.x >= click_area.x1 && point.x <= click_area.x2 && point.y >= click_area.y1 &&
             point.y <= click_area.y2;
  }

  const bool result = accepted && inside;
  click_target = nullptr;
  click_cancelled = false;
  if (result && hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
  return result;
}

static void clear_click_guard(void) {
  click_target = nullptr;
  click_cancelled = false;
}

static void action_card_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  const auto* definition = static_cast<const action_definition*>(lv_event_get_user_data(event));
  if (definition != nullptr) show_action_page(*definition);
}

enum class navigation_action {
  BACK,
  HOME,
  LOG,
  POWER,
};

static constexpr navigation_action back_navigation = navigation_action::BACK;
static constexpr navigation_action home_navigation = navigation_action::HOME;
static constexpr navigation_action log_navigation = navigation_action::LOG;
static constexpr navigation_action power_navigation = navigation_action::POWER;

enum class settings_target {
  LANGUAGE,
  TIMEZONE,
  BRIGHTNESS,
  HAPTICS,
  RECORDING,
  LEGACY,
};

static constexpr settings_target language_target = settings_target::LANGUAGE;
static constexpr settings_target timezone_target = settings_target::TIMEZONE;
static constexpr settings_target brightness_target = settings_target::BRIGHTNESS;
static constexpr settings_target haptics_target = settings_target::HAPTICS;
static constexpr settings_target recording_target = settings_target::RECORDING;
static constexpr settings_target legacy_target = settings_target::LEGACY;

static void navigate_back(void) {
  close_quick_menu();
  if (current_page == page_kind::LANGUAGE || current_page == page_kind::TIMEZONE ||
      current_page == page_kind::BRIGHTNESS || current_page == page_kind::HAPTICS ||
      current_page == page_kind::RECORDING) {
    show_action_page(actions[static_cast<int>(action_id::SETTINGS)]);
  } else if (!home_page_active) {
    show_home_page();
  }
}

static void navigation_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  const auto* action = static_cast<const navigation_action*>(lv_event_get_user_data(event));
  if (action == nullptr) return;

  close_quick_menu();

  if (*action == navigation_action::BACK || *action == navigation_action::HOME) {
    if (*action == navigation_action::HOME) {
      show_home_page();
    } else {
      navigate_back();
    }
  }
}

static void language_option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  const auto* language = static_cast<const app_language*>(lv_event_get_user_data(event));
  if (language == nullptr) return;

  pending_language = *language;
  for (int i = 0; i < 3; ++i) {
    const bool selected = pending_language == static_cast<app_language>(i);
    if (language_option_cards[i] != nullptr) {
      lv_obj_set_style_bg_color(language_option_cards[i],
                                selected ? lv_color_hex(0x347FF1) : ui.card_color, LV_PART_MAIN);
    }
    if (language_check_labels[i] != nullptr)
      lv_label_set_text(language_check_labels[i], selected ? LV_SYMBOL_OK : "");
  }
}

static void apply_language_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  if (settings == nullptr) return;

  if (!settings->set_persistent("tw_language", language_code(pending_language)) ||
      !settings->flush())
    return;

  current_language = pending_language;
  if (status_timer != nullptr) refresh_status_bar(status_timer);
  show_action_page(actions[static_cast<int>(action_id::SETTINGS)]);
}

static void set_choice_style(lv_obj_t* object, bool selected) {
  if (object == nullptr) return;
  const lv_color_t color = selected ? lv_color_hex(0x347FF1) : ui.card_color;
  lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_mix(lv_color_hex(0xFFFFFF), color, 18),
                            LV_STATE_PRESSED);
}

static void refresh_time_choices(void) {
  for (int i = 0; i < 2; ++i) set_choice_style(format_cards[i], (i == 1) == pending_military_time);
  for (int i = 0; i < 24; ++i) set_choice_style(timezone_cards[i], i == pending_timezone_index);
  for (int i = 0; i < 4; ++i) set_choice_style(offset_cards[i], i == pending_offset_index);
  set_choice_style(dst_card, pending_dst);
}

static void time_format_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* index = static_cast<const int*>(lv_event_get_user_data(event));
  if (index == nullptr) return;
  pending_military_time = *index == 1;
  refresh_time_choices();
}

static void timezone_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* index = static_cast<const int*>(lv_event_get_user_data(event));
  if (index == nullptr) return;
  pending_timezone_index = *index;
  refresh_time_choices();
}

static void offset_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* index = static_cast<const int*>(lv_event_get_user_data(event));
  if (index == nullptr) return;
  pending_offset_index = *index;
  refresh_time_choices();
}

static void dst_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  pending_dst = !pending_dst;
  refresh_time_choices();
}

static std::string build_timezone_value() {
  std::string value = timezone_values[pending_timezone_index];
  const size_t separator = value.find(';');
  const std::string zone = value.substr(0, separator);
  const std::string dst_zone =
      separator == std::string::npos ? std::string() : value.substr(separator + 1);

  value = zone;
  if (pending_offset_index != 0) value += ":" + std::string(timezone_offsets[pending_offset_index]);
  if (pending_dst) value += dst_zone;
  return value;
}

static void apply_timezone_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event) || settings == nullptr)
    return;

  const bool saved =
      settings->set_persistent("tw_military_time", pending_military_time ? "1" : "0") &&
      settings->set_persistent("tw_time_zone_guisel", timezone_values[pending_timezone_index]) &&
      settings->set_persistent("tw_time_zone_guioffset", timezone_offsets[pending_offset_index]) &&
      settings->set_persistent("tw_time_zone_guidst", pending_dst ? "1" : "0") &&
      settings->set_persistent("tw_time_zone", build_timezone_value());
  if (saved) {
    settings->update_timezone();
    if (!settings->flush()) return;
    if (status_timer != nullptr) refresh_status_bar(status_timer);
  }
  show_timezone_page();
}

static void request_legacy_gui_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  const int dialog_width = std::min(ui.content_width, ui_px(720));
  const int dialog_height = std::min(ui.height - ui.status_height - ui.nav_height, ui_px(360));
  legacy_dialog = lv_obj_create(lv_layer_top());
  lv_obj_set_size(legacy_dialog, ui.width, ui.height);
  lv_obj_set_pos(legacy_dialog, 0, 0);
  lv_obj_set_style_bg_color(legacy_dialog, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(legacy_dialog, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_width(legacy_dialog, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(legacy_dialog, 0, LV_PART_MAIN);
  disable_scrolling(legacy_dialog);

  lv_obj_t* card = lv_obj_create(legacy_dialog);
  lv_obj_set_size(card, dialog_width, dialog_height);
  lv_obj_center(card);
  set_surface_style(card, ui.card_color);
  lv_obj_set_style_radius(card, std::max(ui_px(1), dialog_height / 8), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, card_inner_padding(), LV_PART_MAIN);
  disable_scrolling(card);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, strings().classic_gui_confirm_title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_color(title, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, ui.text_font, LV_PART_MAIN);

  lv_obj_t* body = lv_label_create(card);
  lv_label_set_text(body, strings().classic_gui_confirm_body);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, dialog_width - card_inner_padding() * 2);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, ui.text_font->line_height + ui_px(20));
  lv_obj_set_style_text_color(body, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(body, ui.status_font, LV_PART_MAIN);

  lv_obj_t* cancel = lv_obj_create(card);
  lv_obj_set_size(cancel, (dialog_width - card_inner_padding() * 2 - ui.card_gap) / 2, ui_px(82));
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  set_surface_style(cancel, ui.background);
  lv_obj_set_style_radius(cancel, ui_px(24), LV_PART_MAIN);
  disable_scrolling(cancel);
  lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
  add_press_cancel_guard(cancel);
  lv_obj_add_event_cb(
      cancel,
      [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED && accept_click(e) && legacy_dialog) {
          lv_obj_delete(legacy_dialog);
          legacy_dialog = nullptr;
        }
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cancel_label = lv_label_create(cancel);
  lv_label_set_text(cancel_label, strings().cancel);
  lv_obj_set_style_text_color(cancel_label, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(cancel_label, ui.text_font, LV_PART_MAIN);
  lv_obj_center(cancel_label);

  lv_obj_t* confirm = lv_obj_create(card);
  lv_obj_set_size(confirm, (dialog_width - card_inner_padding() * 2 - ui.card_gap) / 2, ui_px(82));
  lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  set_surface_style(confirm, lv_color_hex(0x347FF1));
  lv_obj_set_style_radius(confirm, ui_px(24), LV_PART_MAIN);
  disable_scrolling(confirm);
  lv_obj_add_flag(confirm, LV_OBJ_FLAG_CLICKABLE);
  add_press_cancel_guard(confirm);
  lv_obj_add_event_cb(
      confirm,
      [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED && accept_click(e)) {
          switch_to_legacy = true;
          if (legacy_dialog) {
            lv_obj_delete(legacy_dialog);
            legacy_dialog = nullptr;
          }
        }
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* confirm_label = lv_label_create(confirm);
  lv_label_set_text(confirm_label, strings().classic_gui_confirm);
  lv_obj_set_style_text_color(confirm_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(confirm_label, ui.text_font, LV_PART_MAIN);
  lv_obj_center(confirm_label);
}

enum class quick_action {
  SCREENSHOT,
  SCREEN_OFF,
  RECORDING,
};

static constexpr quick_action screenshot_action = quick_action::SCREENSHOT;
static constexpr quick_action screen_off_action = quick_action::SCREEN_OFF;
static constexpr quick_action recording_action = quick_action::RECORDING;

static void set_quick_feedback(const char* title, const char* detail = nullptr) {
  if (quick_feedback == nullptr) return;
  if (detail != nullptr && detail[0] != '\0')
    lv_label_set_text_fmt(quick_feedback, "%s: %s", title, detail);
  else
    lv_label_set_text(quick_feedback, title);
  lv_obj_clear_flag(quick_feedback, LV_OBJ_FLAG_HIDDEN);
}

static void quick_action_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event) || screen == nullptr)
    return;
  const auto* action = static_cast<const quick_action*>(lv_event_get_user_data(event));
  if (action == nullptr) return;

  if (*action == quick_action::SCREENSHOT) {
    // Close before capturing.
    close_quick_menu();
    pending_screenshot = true;
    return;
  }

  if (*action == quick_action::SCREEN_OFF) {
    // Defer blanking until the touch frame is presented.
    close_quick_menu();
    pending_screen_off = true;
    return;
  }

  if (screen->is_recording()) {
    const gui2_backend::capture_result result = screen->stop_recording();
    if (result.success) {
      set_quick_feedback(strings().recording_saved, result.path.c_str());
    } else {
      set_quick_feedback(strings().recording_failed);
    }
  } else {
    const gui2_backend::capture_result result = screen->start_recording();
    if (result.success) {
      set_quick_feedback(strings().recording_started);
    } else {
      set_quick_feedback(strings().recording_failed);
    }
  }
  refresh_recording_ui();
}

static lv_obj_t* create_quick_action_button(lv_obj_t* parent, const lv_image_dsc_t* icon_source,
                                            const char* text, int x, int y, int width,
                                            const quick_action* action) {
  const int height = std::clamp(ui.height / 14, ui_px(124), ui_px(148));
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_pos(button, x, y);
  set_surface_style(button, ui.background);
  lv_obj_set_style_radius(button, height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), ui.background, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(button);
  add_press_cancel_guard(button);
  lv_obj_add_event_cb(button, quick_action_event_cb, LV_EVENT_CLICKED,
                      const_cast<quick_action*>(action));
  lv_obj_add_event_cb(button, quick_panel_gesture_event_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(button, quick_panel_gesture_event_cb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(button, quick_panel_gesture_event_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(button, quick_panel_gesture_event_cb, LV_EVENT_PRESS_LOST, nullptr);

  lv_obj_t* icon = create_svg_image(button, icon_source, ui_px(48), ui_px(48));

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, std::max(1, width - ui_px(12)));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, ui.status_font, LV_PART_MAIN);

  lv_obj_update_layout(icon);
  lv_obj_update_layout(label);
  const int group_gap = std::clamp(ui.card_gap / 2, ui_px(8), ui_px(12));
  const int group_height = lv_obj_get_height(icon) + group_gap + lv_obj_get_height(label);
  const int group_top = std::max(0, (height - group_height) / 2);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, (width - lv_obj_get_width(icon)) / 2, group_top);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, ui_px(6), group_top + lv_obj_get_height(icon) + group_gap);
  return button;
}

static void quick_brightness_event_cb(lv_event_t* event) {
  auto* binding = static_cast<hardware_slider_binding*>(lv_event_get_user_data(event));
  if (binding == nullptr || binding->visual.object == nullptr || hardware == nullptr) return;

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    const int value = gui2_components::get_value(&binding->visual);
    if (!hardware->set_brightness_percent(value)) {
      set_quick_feedback(strings().hardware_error);
      return;
    }
    quick_brightness_dirty = true;
    if (binding->value_label != nullptr) lv_label_set_text_fmt(binding->value_label, "%d%%", value);
    gui2_components::refresh_slider(&binding->visual);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    gui2_components::refresh_slider(&binding->visual);
    if (!quick_brightness_dirty || settings == nullptr) return;
    if (!settings->flush()) {
      set_quick_feedback(strings().hardware_error);
      return;
    }
    quick_brightness_dirty = false;
  }
}

static void sync_quick_brightness(void) {
  if (hardware == nullptr || !hardware->has_brightness() ||
      quick_brightness_binding.visual.object == nullptr)
    return;

  const int value = hardware->brightness_percent();
  gui2_components::set_value(&quick_brightness_binding.visual, value);
  if (quick_brightness_binding.value_label != nullptr)
    lv_label_set_text_fmt(quick_brightness_binding.value_label, "%d%%", value);
  gui2_components::refresh_slider(&quick_brightness_binding.visual);
}

static void refresh_recording_ui(void) {
  if (screen == nullptr) return;
  const bool recording = screen->is_recording();
  if (recording_indicator != nullptr) {
    lv_label_set_text(recording_indicator, strings().recording_indicator);
    if (recording)
      lv_obj_clear_flag(recording_indicator, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(recording_indicator, LV_OBJ_FLAG_HIDDEN);
  }
  if (quick_record_label != nullptr)
    lv_label_set_text(quick_record_label,
                      recording ? strings().stop_recording : strings().start_recording);
  if (quick_record_button != nullptr) {
    lv_obj_set_style_bg_color(quick_record_button,
                              recording ? lv_color_hex(0xF0443E) : ui.background, LV_PART_MAIN);
    lv_obj_set_style_bg_color(quick_record_button,
                              lv_color_mix(lv_color_hex(0xFFFFFF),
                                           recording ? lv_color_hex(0xF0443E) : ui.background, 18),
                              LV_STATE_PRESSED);
  }
  layout_status_labels();
}

static void quick_set_progress(int progress) {
  quick_menu_progress = std::clamp(progress, 0, 1000);
  if (quick_menu != nullptr) {
    const int y = quick_menu_closed_y +
                  (quick_menu_open_y - quick_menu_closed_y) * quick_menu_progress / 1000;
    lv_obj_set_y(quick_menu, y);
    lv_obj_set_style_bg_opa(quick_menu, LV_OPA_COVER, LV_PART_MAIN);
  }
  if (quick_dismiss != nullptr)
    lv_obj_set_style_bg_opa(quick_dismiss, static_cast<lv_opa_t>(quick_menu_progress * 30 / 1000),
                            LV_PART_MAIN);
}

static void quick_menu_anim_exec(void* object, int32_t progress) {
  if (object == quick_menu) quick_set_progress(progress);
}

static void quick_menu_anim_ready(lv_anim_t*) {
  if (!quick_menu_animation_target_open) {
    if (quick_menu != nullptr) lv_obj_add_flag(quick_menu, LV_OBJ_FLAG_HIDDEN);
    if (quick_dismiss != nullptr) lv_obj_add_flag(quick_dismiss, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool quick_menu_input_active(void) {
  return quick_dismiss != nullptr &&
         (!lv_obj_has_flag(quick_dismiss, LV_OBJ_FLAG_HIDDEN) || quick_gesture_tracking);
}

static void reset_wheel_scroll(void) {
  wheel_scroll_velocity = 0.0f;
  wheel_scroll_remainder = 0.0f;
  wheel_scroll_last_ms = 0;
}

static void queue_wheel_scroll(int amount) {
  if (amount == 0 || main_content == nullptr || quick_menu_input_active()) return;

  // Treat the wheel as an impulse. A single wheel notch travels roughly one
  // row, then decelerates naturally instead of teleporting the list.
  const float impulse = static_cast<float>(ui_px(2100));
  wheel_scroll_velocity =
      std::clamp(wheel_scroll_velocity + amount * impulse, -static_cast<float>(ui_px(4200)),
                 static_cast<float>(ui_px(4200)));
  if (wheel_scroll_last_ms == 0) wheel_scroll_last_ms = monotonic_ms();
}

static void advance_wheel_scroll(uint64_t now_ms) {
  if (main_content == nullptr || quick_menu_input_active() || pointer_indev == nullptr ||
      lv_indev_get_state(pointer_indev) == LV_INDEV_STATE_PRESSED) {
    reset_wheel_scroll();
    return;
  }
  if (wheel_scroll_velocity == 0.0f) {
    wheel_scroll_last_ms = now_ms;
    return;
  }

  if (wheel_scroll_last_ms == 0) wheel_scroll_last_ms = now_ms;
  const uint64_t elapsed_ms = std::min<uint64_t>(now_ms - wheel_scroll_last_ms, 50);
  wheel_scroll_last_ms = now_ms;
  if (elapsed_ms == 0) return;

  const float elapsed_s = static_cast<float>(elapsed_ms) / 1000.0f;
  const float distance = wheel_scroll_velocity * elapsed_s + wheel_scroll_remainder;
  const int pixels = static_cast<int>(std::lround(distance));
  wheel_scroll_remainder = distance - pixels;
  if (pixels != 0) lv_obj_scroll_by_bounded(main_content, 0, pixels, LV_ANIM_OFF);

  // Exponential friction is stable across different frame rates. This is a
  // little quicker than the old GUI's long tail while retaining its soft
  // per-frame movement instead of snapping a full wheel step at once.
  wheel_scroll_velocity *= std::exp(-8.0f * elapsed_s);
  if (std::abs(wheel_scroll_velocity) < static_cast<float>(ui_px(8))) reset_wheel_scroll();
}

static void animate_quick_menu(bool open) {
  if (quick_menu == nullptr || quick_dismiss == nullptr) return;
  quick_menu_animation_target_open = open;
  lv_anim_del(quick_menu, quick_menu_anim_exec);
  if (open) {
    refresh_recording_ui();
    sync_quick_brightness();
    lv_obj_clear_flag(quick_dismiss, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(quick_menu, LV_OBJ_FLAG_HIDDEN);
  }

  const int distance = std::abs((open ? 1000 : 0) - quick_menu_progress);
  if (distance == 0) {
    if (!open) quick_menu_anim_ready(nullptr);
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, quick_menu);
  lv_anim_set_values(&animation, quick_menu_progress, open ? 1000 : 0);
  lv_anim_set_duration(&animation, std::clamp(120 + distance / 10, 120, 220));
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, quick_menu_anim_exec);
  lv_anim_set_ready_cb(&animation, quick_menu_anim_ready);
  lv_anim_start(&animation);
}

static void close_quick_menu(void) {
  if (quick_menu != nullptr) lv_anim_del(quick_menu, quick_menu_anim_exec);
  quick_menu_animation_target_open = false;
  quick_set_progress(0);
  if (quick_menu != nullptr) lv_obj_add_flag(quick_menu, LV_OBJ_FLAG_HIDDEN);
  if (quick_dismiss != nullptr) lv_obj_add_flag(quick_dismiss, LV_OBJ_FLAG_HIDDEN);
  quick_gesture_tracking = false;
  quick_gesture_moved = false;
  quick_gesture_from_dismiss = false;
  quick_gesture_velocity_y = 0;
}

static void open_quick_menu(void) {
  animate_quick_menu(true);
}

static void begin_quick_drag(lv_event_t* event, bool from_dismiss) {
  lv_indev_t* indev = lv_event_get_indev(event);
  if (indev == nullptr) indev = pointer_indev;
  if (indev == nullptr) return;

  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_anim_del(quick_menu, quick_menu_anim_exec);
  lv_obj_clear_flag(quick_dismiss, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(quick_menu, LV_OBJ_FLAG_HIDDEN);
  quick_gesture_start_y = point.y;
  quick_gesture_last_y = point.y;
  quick_gesture_last_ms = monotonic_ms();
  quick_gesture_velocity_y = 0;
  quick_gesture_start_progress = quick_menu_progress;
  quick_gesture_moved = false;
  quick_gesture_from_dismiss = from_dismiss;
  quick_gesture_tracking = true;
}

static void update_quick_drag(lv_event_t* event) {
  if (!quick_gesture_tracking) return;
  lv_indev_t* indev = lv_event_get_indev(event);
  if (indev == nullptr) indev = pointer_indev;
  if (indev == nullptr) return;

  lv_point_t point;
  lv_indev_get_point(indev, &point);
  const uint64_t now_ms = monotonic_ms();
  const int delta = point.y - quick_gesture_start_y;
  if (std::abs(delta) >= ui_px(12)) quick_gesture_moved = true;

  const uint64_t elapsed_ms = now_ms - quick_gesture_last_ms;
  if (elapsed_ms > 0)
    quick_gesture_velocity_y =
        static_cast<int>((point.y - quick_gesture_last_y) * 1000 / elapsed_ms);
  quick_gesture_last_y = point.y;
  quick_gesture_last_ms = now_ms;

  const int travel = std::max(1, quick_menu_open_y - quick_menu_closed_y);
  quick_set_progress(quick_gesture_start_progress + delta * 1000 / travel);
}

static void finish_quick_drag(void) {
  quick_gesture_tracking = false;
  const int touch_slop = ui_px(12);
  const int fling_velocity = ui_px(500);
  bool open = quick_menu_progress >= 500;

  if (quick_gesture_from_dismiss) {
    // Outside taps are intentionally ignored. Only an upward gesture closes the menu.
    const bool upward = quick_gesture_start_y - quick_gesture_last_y >= touch_slop;
    const bool upward_fling = quick_gesture_velocity_y <= -fling_velocity;
    open = !(quick_gesture_moved && (upward || upward_fling));
  } else if (quick_gesture_velocity_y >= fling_velocity) {
    open = true;
  } else if (quick_gesture_velocity_y <= -fling_velocity) {
    open = false;
  }

  quick_gesture_from_dismiss = false;
  animate_quick_menu(open);
}

static void status_gesture_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_point_t point;
  lv_indev_t* indev = lv_event_get_indev(event);
  if (indev == nullptr) indev = pointer_indev;
  if (indev == nullptr) return;
  lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    begin_quick_drag(event, false);
  } else if (code == LV_EVENT_PRESSING) {
    update_quick_drag(event);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (quick_gesture_tracking) finish_quick_drag();
  }
}

static void quick_dismiss_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_point_t point;
  lv_indev_t* indev = lv_event_get_indev(event);
  if (indev == nullptr) indev = pointer_indev;
  if (indev == nullptr) return;
  lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    begin_quick_drag(event, true);
  } else if (code == LV_EVENT_PRESSING) {
    update_quick_drag(event);
  } else if (code == LV_EVENT_CLICKED) {
    const bool moved = quick_gesture_moved;
    // Consume the click guard state, but never close or vibrate for an outside tap.
    clear_click_guard();
    if (!moved) quick_gesture_tracking = false;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (quick_gesture_tracking) finish_quick_drag();
  }
}

static void quick_panel_gesture_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_indev_t* indev = lv_event_get_indev(event);
  if (indev == nullptr) indev = pointer_indev;
  if (indev == nullptr) return;

  lv_point_t point;
  lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    begin_quick_drag(event, false);
  } else if (code == LV_EVENT_PRESSING) {
    update_quick_drag(event);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (quick_gesture_tracking) finish_quick_drag();
  }
}

static void create_quick_menu(void) {
  quick_dismiss = lv_obj_create(lv_layer_top());
  lv_obj_set_size(quick_dismiss, ui.width, ui.height);
  lv_obj_set_pos(quick_dismiss, 0, 0);
  set_surface_style(quick_dismiss, lv_color_hex(0x000000), LV_OPA_30);
  lv_obj_set_style_pad_all(quick_dismiss, 0, LV_PART_MAIN);
  lv_obj_add_flag(quick_dismiss, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(quick_dismiss);
  add_press_cancel_guard(quick_dismiss);
  lv_obj_add_event_cb(quick_dismiss, quick_dismiss_event_cb, LV_EVENT_ALL, nullptr);

  quick_menu = lv_obj_create(lv_layer_top());
  lv_obj_set_size(quick_menu, ui.content_width, 1);
  set_surface_style(quick_menu, ui.card_color);
  lv_obj_set_style_radius(quick_menu, ui_px(28), LV_PART_MAIN);
  lv_obj_set_style_pad_all(quick_menu, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(quick_menu, ui_px(12), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(quick_menu, 48, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(quick_menu, ui_px(4), LV_PART_MAIN);
  lv_obj_add_flag(quick_menu, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(quick_menu, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(quick_menu);
  lv_obj_add_event_cb(quick_menu, quick_panel_gesture_event_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(quick_menu, quick_panel_gesture_event_cb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(quick_menu, quick_panel_gesture_event_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(quick_menu, quick_panel_gesture_event_cb, LV_EVENT_PRESS_LOST, nullptr);

  lv_obj_t* title = lv_label_create(quick_menu);
  lv_label_set_text(title, strings().quick_menu_title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, card_inner_padding(), card_inner_padding());
  lv_obj_set_style_text_color(title, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, ui.text_font, LV_PART_MAIN);

  const int inner_padding = card_inner_padding();
  const int gap = ui.card_gap;
  lv_obj_update_layout(title);
  int content_top = inner_padding + lv_obj_get_height(title) + gap;

  quick_brightness_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, true, false };
  quick_brightness_dirty = false;
  if (hardware != nullptr && hardware->has_brightness()) {
    const int header_height = std::max(1, ui.status_font->line_height);
    const int slider_height = std::clamp(std::min(ui.width, ui.height) / 24, ui_px(28), ui_px(48));
    const int slider_gap = std::clamp(ui.card_gap / 2, ui_px(8), ui_px(12));

    lv_obj_t* brightness_label = lv_label_create(quick_menu);
    lv_label_set_text(brightness_label, strings().brightness_label);
    lv_obj_set_pos(brightness_label, inner_padding, content_top);
    lv_obj_set_style_text_color(brightness_label, ui.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness_label, ui.status_font, LV_PART_MAIN);

    quick_brightness_binding.value_label = lv_label_create(quick_menu);
    lv_obj_set_width(quick_brightness_binding.value_label, ui.content_width - inner_padding * 2);
    lv_obj_set_style_text_align(quick_brightness_binding.value_label, LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN);
    lv_obj_set_pos(quick_brightness_binding.value_label, inner_padding, content_top);
    lv_obj_set_style_text_color(quick_brightness_binding.value_label, ui.secondary_text,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(quick_brightness_binding.value_label, ui.status_font, LV_PART_MAIN);

    const lv_color_t slider_background = lv_color_mix(lv_color_hex(0xFFFFFF), ui.card_color, 38);
    lv_obj_t* slider = gui2_components::create_slider(
        quick_menu, inner_padding, content_top + header_height + slider_gap,
        ui.content_width - inner_padding * 2, slider_height, 10, 100,
        hardware->brightness_percent(), slider_background, lv_color_hex(0x347FF1),
        lv_color_hex(0xFFFFFF), &quick_brightness_binding.visual);
    if (slider != nullptr) {
      lv_obj_add_event_cb(slider, quick_brightness_event_cb, LV_EVENT_VALUE_CHANGED,
                          &quick_brightness_binding);
      lv_obj_add_event_cb(slider, quick_brightness_event_cb, LV_EVENT_RELEASED,
                          &quick_brightness_binding);
      lv_obj_add_event_cb(slider, quick_brightness_event_cb, LV_EVENT_PRESS_LOST,
                          &quick_brightness_binding);
      lv_obj_add_event_cb(slider, hardware_slider_state_event_cb, LV_EVENT_PRESSED,
                          &quick_brightness_binding);
      lv_obj_add_event_cb(slider, hardware_slider_state_event_cb, LV_EVENT_PRESSING,
                          &quick_brightness_binding);
    }
    content_top += header_height + slider_gap + slider_height + gap;
    sync_quick_brightness();
  }

  struct quick_action_entry {
    const lv_image_dsc_t* icon;
    const char* text;
    const quick_action* action;
  };
  quick_action_entry entries[3];
  int action_count = 0;
  entries[action_count++] = { &kGui2IconScrShot, strings().screenshot, &screenshot_action };
  if (screen != nullptr && screen->has_screen_off())
    entries[action_count++] = { &kGui2IconPower, strings().screen_off, &screen_off_action };
  entries[action_count++] = { &kGui2IconScrRecord, strings().start_recording, &recording_action };

  const int action_height = std::clamp(ui.height / 14, ui_px(124), ui_px(148));
  const int columns = std::min(3, action_count);
  const int available_width = ui.content_width - inner_padding * 2;
  const int button_width = (available_width - gap * (columns - 1)) / columns;
  const int row_width = button_width * action_count + gap * (action_count - 1);
  const int row_left = inner_padding + std::max(0, (available_width - row_width) / 2);
  for (int i = 0; i < action_count; ++i) {
    const int x = row_left + i * (button_width + gap);
    lv_obj_t* button = create_quick_action_button(quick_menu, entries[i].icon, entries[i].text, x,
                                                  content_top, button_width, entries[i].action);
    if (*entries[i].action == quick_action::RECORDING) {
      quick_record_button = button;
      quick_record_label = button == nullptr ? nullptr : lv_obj_get_child(button, 1);
    }
  }
  content_top += action_height;

  quick_feedback = lv_label_create(quick_menu);
  const int feedback_height = std::max(ui_px(36), ui.status_font->line_height * 2);
  const int feedback_gap = std::clamp(ui.card_gap / 2, ui_px(8), ui_px(12));
  quick_menu_height = content_top + feedback_gap + feedback_height + inner_padding;
  lv_obj_set_size(quick_menu, ui.content_width, quick_menu_height);
  lv_obj_set_style_radius(quick_menu, std::clamp(quick_menu_height / 4, ui_px(24), ui_px(40)),
                          LV_PART_MAIN);
  lv_obj_set_width(quick_feedback, ui.content_width - inner_padding * 2);
  lv_obj_set_height(quick_feedback, feedback_height);
  lv_obj_set_pos(quick_feedback, inner_padding, content_top + feedback_gap);
  lv_label_set_long_mode(quick_feedback, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(quick_feedback, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  lv_obj_set_style_text_color(quick_feedback, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(quick_feedback, ui.status_font, LV_PART_MAIN);
  lv_obj_add_flag(quick_feedback, LV_OBJ_FLAG_HIDDEN);
  disable_scrolling(quick_feedback);

  quick_menu_open_y = ui.status_height + std::max(ui_px(8), ui.card_gap / 2);
  quick_menu_closed_y = -quick_menu_height;
  lv_obj_set_pos(quick_menu, ui.outer_margin, quick_menu_closed_y);

  screenshot_flash = lv_obj_create(lv_layer_top());
  lv_obj_set_size(screenshot_flash, ui.width, ui.height);
  lv_obj_set_pos(screenshot_flash, 0, 0);
  set_surface_style(screenshot_flash, lv_color_hex(0xFFFFFF), LV_OPA_30);
  lv_obj_set_style_pad_all(screenshot_flash, 0, LV_PART_MAIN);
  lv_obj_clear_flag(screenshot_flash, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(screenshot_flash);
  lv_obj_add_flag(screenshot_flash, LV_OBJ_FLAG_HIDDEN);

  quick_set_progress(0);
  close_quick_menu();
}

static void create_mouse_cursor(void) {
  if (pointer_indev == nullptr || !ev_has_mouse()) return;

  mouse_cursor = lv_image_create(lv_layer_sys());
  if (mouse_cursor == nullptr) return;
  lv_obj_set_size(mouse_cursor, ui_px(48), ui_px(48));
  lv_image_set_src(mouse_cursor, gui2_svg_get_raster(&kGui2IconCursor, ui_px(48), ui_px(48)));
  lv_image_set_inner_align(mouse_cursor, LV_IMAGE_ALIGN_CONTAIN);
  lv_indev_set_cursor(pointer_indev, mouse_cursor);
}

static void show_screenshot_flash(void) {
  if (screenshot_flash == nullptr) return;
  lv_obj_clear_flag(screenshot_flash, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(screenshot_flash);
  screenshot_flash_until_ms = monotonic_ms() + 70;
}

static void update_screenshot_flash(uint64_t now_ms) {
  if (screenshot_flash == nullptr || screenshot_flash_until_ms == 0) return;
  if (now_ms >= screenshot_flash_until_ms) {
    lv_obj_add_flag(screenshot_flash, LV_OBJ_FLAG_HIDDEN);
    screenshot_flash_until_ms = 0;
  }
}

static void process_pending_screen_actions(void) {
  if (screen == nullptr) return;

  if (pending_screenshot) {
    pending_screenshot = false;
    const gui2_backend::capture_result result = screen->save_screenshot();
    if (result.success) show_screenshot_flash();
  }

  if (pending_screen_off) {
    pending_screen_off = false;
    if (screen->screen_off()) gui2_input_set_screen_off(true);
  }
}

static int card_inner_padding(void) {
  return std::clamp(ui.outer_margin, ui_px(32), ui_px(56));
}

static int single_line_card_height() {
  return std::clamp(ui.card_height * 2 / 3, ui_px(96), ui_px(148));
}

static lv_obj_t* create_action_card(lv_obj_t* parent, const action_definition& definition,
                                    int card_width, int card_height, int icon_size,
                                    const lv_font_t* card_font, lv_color_t card_color,
                                    lv_color_t text_color, lv_color_t secondary_color) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, card_width, card_height);
  disable_scrolling(card);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, card_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_hex(0xFFFFFF), card_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, ui_px(3), LV_PART_MAIN);
  add_press_cancel_guard(card);
  lv_obj_add_event_cb(card, action_card_event_cb, LV_EVENT_CLICKED,
                      const_cast<action_definition*>(&definition));

  const int card_side_padding = card_inner_padding();
  const int title_gap = std::clamp(card_height / 8, ui_px(20), ui_px(28));

  lv_obj_t* icon = lv_obj_create(card);
  lv_obj_set_size(icon, icon_size, icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, card_side_padding, 0);
  lv_obj_set_style_radius(icon, icon_size / 4, LV_PART_MAIN);
  set_surface_style(icon, lv_color_hex(definition.color));
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(icon);

  const int icon_art_size = action_icon_art_size(icon_size);
  lv_obj_t* icon_image = create_svg_image(icon, definition.icon, icon_art_size, icon_art_size);
  lv_obj_center(icon_image);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, strings().actions[static_cast<int>(definition.id)].title);
  lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  const int title_left = card_side_padding + icon_size + title_gap;
  const int title_width = std::max(1, card_width - title_left - card_side_padding - ui_px(36));
  lv_obj_set_width(title, title_width);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, title_left, 0);
  lv_obj_set_style_text_color(title, text_color, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, card_font, LV_PART_MAIN);

  lv_obj_t* arrow = create_svg_image(card, &kGui2IconArrowRight, ui_px(48), ui_px(48));
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -card_side_padding, 0);

  return card;
}

static lv_obj_t* create_nav_button(lv_obj_t* parent, const lv_image_dsc_t* icon_source, int size,
                                   bool circular, lv_color_t nav_color, lv_color_t text_color,
                                   const navigation_action& action, int width = -1, int height = -1,
                                   bool embedded = false) {
  lv_obj_t* button = lv_obj_create(parent);
  const int button_width = width > 0 ? width : size;
  const int button_height = height > 0 ? height : size;
  lv_obj_set_size(button, button_width, button_height);
  disable_scrolling(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  if (embedded) {
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
  } else {
    const int radius_size = std::min(button_width, button_height);
    lv_obj_set_style_radius(button, circular ? radius_size / 2 : radius_size / 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, nav_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), nav_color, 18),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, ui_px(4), LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x3B3B3B), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  }
  if (embedded) lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  add_press_cancel_guard(button);
  lv_obj_add_event_cb(button, navigation_event_cb, LV_EVENT_CLICKED,
                      const_cast<navigation_action*>(&action));

  const int icon_size = std::clamp(button_height * 58 / 100, ui_px(52), ui_px(84));
  lv_obj_t* image = create_svg_image(button, icon_source, icon_size, icon_size);
  lv_obj_center(image);
  return button;
}

static void reset_page_layer(void) {
  if (page_layer != nullptr) lv_obj_clean(page_layer);

  main_content = nullptr;
  status_hint = nullptr;
  heading = nullptr;
  for (int i = 0; i < 3; ++i) {
    language_option_cards[i] = nullptr;
    language_check_labels[i] = nullptr;
  }
}

static void create_page_heading(const char* title, const char* summary) {
  heading = lv_obj_create(page_layer);
  lv_obj_set_pos(heading, ui.outer_margin, ui.heading_top);
  lv_obj_set_size(heading, ui.content_width, ui.heading_height);
  set_surface_style(heading, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(heading, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(heading, ui_px(10), LV_PART_MAIN);
  disable_scrolling(heading);

  lv_obj_t* title_label = lv_label_create(heading);
  lv_label_set_text(title_label, title);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, ui_px(2));
  lv_obj_set_style_text_color(title_label, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title_label, ui.brand_font, LV_PART_MAIN);

  lv_obj_t* version = lv_label_create(heading);
  lv_label_set_text(version, "4.0.0");
  lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -ui_px(4), ui_px(10));
  lv_obj_set_style_text_color(version, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(version, ui.status_font, LV_PART_MAIN);

  status_hint = lv_label_create(heading);
  lv_label_set_text(status_hint, summary);
  lv_obj_align(status_hint, LV_ALIGN_BOTTOM_LEFT, 0, -ui_px(6));
  lv_obj_set_style_text_color(status_hint, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(status_hint, ui.status_font, LV_PART_MAIN);
}

static void create_scroll_area(int bottom_reserved = 0) {
  const int scroll_top = ui.heading_top + ui.heading_height + ui.cards_top_gap;

  main_content = lv_obj_create(page_layer);
  lv_obj_set_pos(main_content, 0, scroll_top);
  lv_obj_set_size(main_content, ui.width, std::max(1, ui.height - ui.status_height - scroll_top));
  set_surface_style(main_content, ui.background);
  lv_obj_add_flag(main_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(main_content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(main_content, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(main_content, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_style_pad_all(main_content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(main_content, bottom_reserved, LV_PART_MAIN);
  create_navigation_fade();
}

static void create_page_scaffold(page_kind page, bool is_home, const char* title,
                                 const char* summary, int bottom_reserved = 0) {
  close_quick_menu();
  reset_page_layer();
  home_page_active = is_home;
  home_navigation_active = true;
  current_page = page;
  create_page_heading(title, summary);
  create_scroll_area(bottom_reserved);
}

static void refresh_navigation_icons(void) {
  if (nav_home_icon != nullptr)
    lv_image_set_src(
        nav_home_icon,
        gui2_svg_get_raster(home_navigation_active ? &kGui2IconHomeFilled : &kGui2IconHomeOutlined,
                            lv_obj_get_width(nav_home_icon), lv_obj_get_height(nav_home_icon)));
  if (nav_console_icon != nullptr)
    lv_image_set_src(nav_console_icon, gui2_svg_get_raster(&kGui2IconConsoleOutline,
                                                           lv_obj_get_width(nav_console_icon),
                                                           lv_obj_get_height(nav_console_icon)));
  if (nav_power_icon != nullptr)
    lv_image_set_src(nav_power_icon,
                     gui2_svg_get_raster(&kGui2IconPower, lv_obj_get_width(nav_power_icon),
                                         lv_obj_get_height(nav_power_icon)));
}

static void settings_option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* target = static_cast<const settings_target*>(lv_event_get_user_data(event));
  if (target == nullptr) return;
  if (*target == settings_target::LANGUAGE)
    show_language_page();
  else if (*target == settings_target::TIMEZONE)
    show_timezone_page();
  else if (*target == settings_target::BRIGHTNESS)
    show_brightness_page();
  else if (*target == settings_target::HAPTICS)
    show_haptics_page();
  else if (*target == settings_target::RECORDING)
    show_recording_page();
  else
    request_legacy_gui_event_cb(event);
}

static lv_obj_t* create_setting_option(lv_obj_t* parent, const char* title, const char* detail,
                                       const settings_target* target) {
  int option_height = std::max(ui.card_height, ui_px(132));
  lv_obj_t* option = lv_obj_create(parent);
  lv_obj_set_size(option, ui.content_width, option_height);
  lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(option, option_height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(option, ui.card_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(option, lv_color_mix(lv_color_hex(0xFFFFFF), ui.card_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(option, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(option, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(option, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(option, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(option, ui_px(3), LV_PART_MAIN);
  disable_scrolling(option);
  add_press_cancel_guard(option);
  lv_obj_add_event_cb(option, settings_option_event_cb, LV_EVENT_CLICKED,
                      const_cast<settings_target*>(target));

  const int text_left = card_inner_padding();
  const int text_gap = ui_px(8);
  const int text_right = card_inner_padding() + ui_px(56);
  const int text_width = std::max(1, ui.content_width - text_left - text_right);

  lv_obj_t* text_block = lv_obj_create(option);
  lv_obj_set_width(text_block, text_width);
  lv_obj_set_height(text_block, LV_SIZE_CONTENT);
  set_surface_style(text_block, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(text_block, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(text_block, text_gap, LV_PART_MAIN);
  lv_obj_set_layout(text_block, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(text_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(text_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(text_block, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(text_block);

  lv_obj_t* option_title = lv_label_create(text_block);
  lv_label_set_text(option_title, title);
  lv_label_set_long_mode(option_title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(option_title, text_width);
  lv_obj_set_style_text_color(option_title, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(option_title, ui.text_font, LV_PART_MAIN);

  lv_obj_t* option_detail = lv_label_create(text_block);
  lv_label_set_text(option_detail, detail);
  lv_label_set_long_mode(option_detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(option_detail, text_width);
  lv_obj_set_style_text_color(option_detail, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(option_detail, ui.status_font, LV_PART_MAIN);

  lv_obj_update_layout(text_block);
  option_height = std::max(option_height, lv_obj_get_height(text_block) + ui_px(32));
  lv_obj_set_height(option, option_height);
  lv_obj_align(text_block, LV_ALIGN_LEFT_MID, text_left, 0);

  lv_obj_t* arrow = create_svg_image(option, &kGui2IconArrowRight, ui_px(32), ui_px(32));
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -card_inner_padding(), 0);

  return option;
}

static void refresh_language_options(void) {
  for (int i = 0; i < 3; ++i) {
    const bool selected = pending_language == static_cast<app_language>(i);
    const lv_color_t option_color = selected ? lv_color_hex(0x347FF1) : ui.card_color;
    if (language_option_cards[i] != nullptr) {
      lv_obj_set_style_bg_color(language_option_cards[i], option_color, LV_PART_MAIN);
      lv_obj_set_style_bg_color(language_option_cards[i],
                                lv_color_mix(lv_color_hex(0xFFFFFF), option_color, 18),
                                LV_STATE_PRESSED);
    }
    if (language_check_labels[i] != nullptr)
      lv_label_set_text(language_check_labels[i], selected ? LV_SYMBOL_OK : "");
  }
}

static void create_language_option(lv_obj_t* parent, int index, app_language language) {
  const int option_height = std::max(ui.card_height, ui_px(118));
  lv_obj_t* option = lv_obj_create(parent);
  lv_obj_set_size(option, ui.content_width, option_height);
  lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(option, option_height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(option, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(option, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(option, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(option, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(option, ui_px(3), LV_PART_MAIN);
  disable_scrolling(option);
  add_press_cancel_guard(option);
  lv_obj_add_event_cb(option, language_option_event_cb, LV_EVENT_CLICKED,
                      const_cast<app_language*>(&language_values[index]));

  language_option_cards[index] = option;
  const bool selected = pending_language == language;
  const lv_color_t option_color = selected ? lv_color_hex(0x347FF1) : ui.card_color;
  lv_obj_set_style_bg_color(option, option_color, LV_PART_MAIN);
  lv_obj_set_style_bg_color(option, lv_color_mix(lv_color_hex(0xFFFFFF), option_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t* label = lv_label_create(option);
  lv_label_set_text(label, language_name(language));
  lv_obj_align(label, LV_ALIGN_LEFT_MID, card_inner_padding(), 0);
  lv_obj_set_style_text_color(label, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, ui.text_font, LV_PART_MAIN);

  language_check_labels[index] = lv_label_create(option);
  lv_obj_align(language_check_labels[index], LV_ALIGN_RIGHT_MID, -card_inner_padding(), 0);
  lv_obj_set_style_text_color(language_check_labels[index], ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(language_check_labels[index], &lv_font_montserrat_48, LV_PART_MAIN);
  scale_icon_font(language_check_labels[index]);
  lv_label_set_text(language_check_labels[index], pending_language == language ? LV_SYMBOL_OK : "");
}

static lv_obj_t* create_choice_card(lv_obj_t* parent, const char* label, int width, int height,
                                    lv_event_cb_t callback, const void* user_data) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, width, height);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(card, height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, ui_px(3), LV_PART_MAIN);
  disable_scrolling(card);
  add_press_cancel_guard(card);
  lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, const_cast<void*>(user_data));

  lv_obj_t* text = lv_label_create(card);
  lv_label_set_text(text, label);
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(text, std::max(1, width - card_inner_padding() * 2));
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(text, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(text, ui.text_font, LV_PART_MAIN);
  lv_obj_update_layout(text);
  lv_obj_center(text);
  return card;
}

static lv_obj_t* create_section_label(lv_obj_t* parent, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, ui.content_width);
  lv_obj_set_style_text_color(label, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, ui.status_font, LV_PART_MAIN);
  return label;
}

static void show_hardware_error(const char* text) {
  if (hardware_error_label == nullptr) return;
  lv_label_set_text(hardware_error_label, text);
  lv_obj_clear_flag(hardware_error_label, LV_OBJ_FLAG_HIDDEN);
}

static void clear_hardware_error(void) {
  if (hardware_error_label != nullptr) lv_obj_add_flag(hardware_error_label, LV_OBJ_FLAG_HIDDEN);
}

static void update_hardware_slider_value(const hardware_slider_binding& binding, int value) {
  if (binding.value_label == nullptr) return;
  if (binding.recording_fps) {
    lv_label_set_text_fmt(binding.value_label, "%d FPS", recording_fps_at(value));
  } else if (binding.brightness) {
    lv_label_set_text_fmt(binding.value_label, "%d%%", value);
  } else {
    lv_label_set_text_fmt(binding.value_label, "%d ms", value);
  }
}

static void hardware_slider_state_event_cb(lv_event_t* event) {
  auto* binding = static_cast<hardware_slider_binding*>(lv_event_get_user_data(event));
  if (binding != nullptr) gui2_components::refresh_slider(&binding->visual);
}

static void hardware_slider_event_cb(lv_event_t* event) {
  auto* binding = static_cast<hardware_slider_binding*>(lv_event_get_user_data(event));
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (binding == nullptr || slider == nullptr || (hardware == nullptr && !binding->recording_fps))
    return;

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    const int value = gui2_components::get_value(&binding->visual);
    bool applied = false;
    if (binding->recording_fps) {
      applied = screen != nullptr && screen->set_recording_fps(recording_fps_at(value));
    } else {
      applied = binding->brightness ? hardware->set_brightness_percent(value)
                                    : hardware->set_haptic_duration_ms(binding->channel, value);
    }
    if (!applied) {
      show_hardware_error(strings().hardware_error);
      return;
    }
    hardware_settings_dirty = true;
    update_hardware_slider_value(*binding, value);
    gui2_components::refresh_slider(&binding->visual);
    clear_hardware_error();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    gui2_components::refresh_slider(&binding->visual);
    if (!hardware_settings_dirty || settings == nullptr) return;
    if (!settings->flush()) {
      show_hardware_error(strings().hardware_error);
      return;
    }
    hardware_settings_dirty = false;
  }
}

static lv_obj_t* create_hardware_slider(lv_obj_t* parent, const char* label, int minimum,
                                        int maximum, int value, hardware_slider_binding* binding) {
  const int side_padding = card_inner_padding();
  const int header_height = std::max(1, ui.text_font->line_height);
  const int content_gap = std::clamp(ui.card_gap, ui_px(12), ui_px(18));
  const int slider_height = std::clamp(std::min(ui.width, ui.height) / 19, ui_px(28), ui_px(56));
  const int content_height = header_height + content_gap + slider_height;
  const int card_height = std::max(ui.card_height, side_padding * 2 + content_height);
  const int card_width = ui.content_width;
  const int content_width = std::max(1, card_width - side_padding * 2);
  const int inner_height = std::max(1, card_height - side_padding * 2);
  const int content_top = side_padding + std::max(0, (inner_height - content_height) / 2);
  const int slider_top = content_top + header_height + content_gap;

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, card_width, card_height);
  set_surface_style(card, ui.card_color);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, ui_px(3), LV_PART_MAIN);
  lv_obj_add_flag(card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  disable_scrolling(card);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, label);
  lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(title, content_width * 65 / 100);
  lv_obj_set_pos(title, side_padding, content_top);
  lv_obj_set_style_text_color(title, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, ui.text_font, LV_PART_MAIN);

  binding->value_label = lv_label_create(card);
  lv_obj_set_width(binding->value_label, content_width * 35 / 100);
  lv_obj_set_style_text_align(binding->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_pos(binding->value_label, card_width - side_padding - content_width * 35 / 100,
                 content_top);
  lv_obj_set_style_text_color(binding->value_label, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(binding->value_label, ui.text_font, LV_PART_MAIN);

  const lv_color_t slider_background = lv_color_mix(lv_color_hex(0xFFFFFF), ui.card_color, 38);
  lv_obj_t* slider = gui2_components::create_slider(
      card, side_padding, slider_top, content_width, slider_height, minimum, maximum, value,
      slider_background, lv_color_hex(0x347FF1), lv_color_hex(0xFFFFFF), &binding->visual);
  if (slider == nullptr) return card;
  lv_obj_add_event_cb(slider, hardware_slider_event_cb, LV_EVENT_VALUE_CHANGED, binding);
  lv_obj_add_event_cb(slider, hardware_slider_state_event_cb, LV_EVENT_PRESSED, binding);
  lv_obj_add_event_cb(slider, hardware_slider_state_event_cb, LV_EVENT_PRESSING, binding);
  lv_obj_add_event_cb(slider, hardware_slider_event_cb, LV_EVENT_RELEASED, binding);
  lv_obj_add_event_cb(slider, hardware_slider_event_cb, LV_EVENT_PRESS_LOST, binding);
  update_hardware_slider_value(*binding, gui2_components::get_value(&binding->visual));
  gui2_components::refresh_slider(&binding->visual);
  return card;
}

static lv_obj_t* create_hardware_body(void) {
  lv_obj_t* body = lv_obj_create(main_content);
  lv_obj_set_pos(body, ui.outer_margin, 0);
  lv_obj_set_width(body, ui.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  set_surface_style(body, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_left(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_right(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(body, ui_px(8), LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(body, navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_add_flag(body, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  disable_scrolling(body);
  hardware_error_label = create_section_label(body, "");
  lv_obj_set_style_text_color(hardware_error_label, lv_color_hex(0xF0443E), LV_PART_MAIN);
  lv_obj_add_flag(hardware_error_label, LV_OBJ_FLAG_HIDDEN);
  return body;
}

static void show_brightness_page(void) {
  hardware_settings_dirty = false;
  brightness_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, true, false };
  create_page_scaffold(page_kind::BRIGHTNESS, false, strings().brightness_title,
                       strings().brightness_summary);
  lv_obj_t* body = create_hardware_body();
  const int value = hardware == nullptr ? 20 : hardware->brightness_percent();
  create_hardware_slider(body, strings().brightness_label, 10, 100, value, &brightness_binding);
  lv_obj_update_layout(body);
}

static void show_haptics_page(void) {
  hardware_settings_dirty = false;
  for (auto& binding : haptic_bindings)
    binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false, false };
  create_page_scaffold(page_kind::HAPTICS, false, strings().haptics_title,
                       strings().haptics_summary);
  lv_obj_t* body = create_hardware_body();
  const gui2_backend::haptic_channel channels[3] = {
    gui2_backend::haptic_channel::BUTTON,
    gui2_backend::haptic_channel::KEYBOARD,
    gui2_backend::haptic_channel::ACTION,
  };
  const char* labels[3] = { strings().button_haptics, strings().keyboard_haptics,
                            strings().action_haptics };
  for (int i = 0; i < 3; ++i) {
    haptic_bindings[i].channel = channels[i];
    const int maximum = i == 2 ? 500 : 300;
    const int value = hardware == nullptr ? 0 : hardware->haptic_duration_ms(channels[i]);
    create_hardware_slider(body, labels[i], 0, maximum, value, &haptic_bindings[i]);
  }
  lv_obj_update_layout(body);
}

static void show_recording_page(void) {
  hardware_settings_dirty = false;
  recording_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false, true };
  create_page_scaffold(page_kind::RECORDING, false, strings().recording_settings_title,
                       strings().recording_settings_summary);
  lv_obj_t* body = create_hardware_body();

  int fps = screen == nullptr ? 30 : screen->recording_fps();
  int index = 2;
  for (int i = 0; i < recording_fps_count(); ++i) {
    if (recording_fps_at(i) == fps) {
      index = i;
      break;
    }
  }
  create_hardware_slider(body, strings().recording_fps_label, 0, recording_fps_count() - 1, index,
                         &recording_binding);
  lv_obj_update_layout(body);
}

static void show_timezone_page(void) {
  pending_military_time = settings != nullptr && settings->get_int("tw_military_time", 0) != 0;
  pending_dst = settings != nullptr && settings->get_int("tw_time_zone_guidst", 0) != 0;
  pending_offset_index = 0;
  if (settings != nullptr) {
    const std::string offset = settings->get_string("tw_time_zone_guioffset", "0");
    for (int i = 0; i < 4; ++i) {
      if (offset == timezone_offsets[i]) {
        pending_offset_index = i;
        break;
      }
    }
  }

  const std::string selected_zone =
      settings == nullptr ? timezone_values[5]
                          : settings->get_string("tw_time_zone_guisel", timezone_values[5]);
  pending_timezone_index = 5;
  for (int i = 0; i < 24; ++i) {
    if (selected_zone == timezone_values[i]) {
      pending_timezone_index = i;
      break;
    }
  }

  const int button_height = single_line_card_height();
  const int bottom_reserved = button_height + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::TIMEZONE, false, strings().time_title, strings().time_summary,
                       bottom_reserved);

  lv_obj_t* body = lv_obj_create(main_content);
  lv_obj_set_pos(body, ui.outer_margin, 0);
  lv_obj_set_width(body, ui.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  set_surface_style(body, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  disable_scrolling(body);
  lv_obj_set_style_pad_bottom(body, navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  create_section_label(body, strings().time_format);
  lv_obj_t* format_row = lv_obj_create(body);
  const int choice_height = single_line_card_height();
  const int row_padding = ui_px(10);
  const int row_content_width = std::max(1, ui.content_width - row_padding * 2);
  lv_obj_set_size(format_row, ui.content_width, choice_height + row_padding * 2);
  set_surface_style(format_row, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(format_row, row_padding, LV_PART_MAIN);
  lv_obj_set_style_pad_column(format_row, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(format_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(format_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(format_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  disable_scrolling(format_row);
  lv_obj_add_flag(format_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  format_cards[0] =
      create_choice_card(format_row, strings().twelve_hour, (row_content_width - ui.card_gap) / 2,
                         choice_height, time_format_event_cb, &format_indices[0]);
  format_cards[1] =
      create_choice_card(format_row, strings().twenty_four_hour,
                         row_content_width - (row_content_width - ui.card_gap) / 2 - ui.card_gap,
                         choice_height, time_format_event_cb, &format_indices[1]);

  create_section_label(body, strings().select_timezone);
  for (int i = 0; i < 24; ++i) {
    timezone_cards[i] = create_choice_card(body, strings().timezone_names[i], ui.content_width,
                                           choice_height, timezone_event_cb, &timezone_indices[i]);
  }

  create_section_label(body, strings().timezone_offset);
  lv_obj_t* offset_row = lv_obj_create(body);
  lv_obj_set_size(offset_row, ui.content_width, choice_height * 2 + ui.card_gap + row_padding * 2);
  set_surface_style(offset_row, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(offset_row, row_padding, LV_PART_MAIN);
  lv_obj_set_style_pad_column(offset_row, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(offset_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(offset_row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(offset_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(offset_row, ui.card_gap, LV_PART_MAIN);
  disable_scrolling(offset_row);
  lv_obj_add_flag(offset_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  const int offset_width = (row_content_width - ui.card_gap) / 2;
  const char* offset_labels[4] = { strings().offset_none, strings().offset_15, strings().offset_30,
                                   strings().offset_45 };
  for (int i = 0; i < 4; ++i) {
    offset_cards[i] = create_choice_card(offset_row, offset_labels[i], offset_width, choice_height,
                                         offset_event_cb, &offset_indices[i]);
  }

  dst_card = create_choice_card(body, strings().use_dst, ui.content_width, choice_height,
                                dst_event_cb, nullptr);

  current_timezone_label = create_section_label(body, "");
  const std::string current = settings == nullptr
                                  ? "CST6CDT,M3.2.0,M11.1.0"
                                  : settings->get_string("tw_time_zone", "CST6CDT,M3.2.0,M11.1.0");
  std::string current_text = std::string(strings().current_timezone) + ": " + current;
  lv_label_set_text(current_timezone_label, current_text.c_str());

  refresh_time_choices();
  create_apply_button(apply_timezone_event_cb, strings().apply);
}

static void show_home_page(void) {
  create_page_scaffold(page_kind::HOME, true, "YARP", strings().home_summary);

  const bool landscape = ui.width > ui.height;
  const int columns = landscape && ui.width >= 800 ? 2 : 1;
  const int card_width = (ui.content_width - ui.card_gap * (columns - 1)) / columns;

  lv_obj_t* cards = lv_obj_create(main_content);
  lv_obj_set_pos(cards, ui.outer_margin, 0);
  lv_obj_set_width(cards, ui.content_width);
  lv_obj_set_height(cards, LV_SIZE_CONTENT);
  set_surface_style(cards, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(cards, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(cards, navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(cards, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_style_pad_column(cards, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(cards, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(cards, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  disable_scrolling(cards);

  for (const action_definition& definition : actions) {
    create_action_card(cards, definition, card_width, ui.card_height, ui.icon_size, ui.text_font,
                       ui.card_color, ui.primary_text, ui.secondary_text);
  }
}

static void show_action_page(const action_definition& definition) {
  const auto& action_text = strings().actions[static_cast<int>(definition.id)];
  create_page_scaffold(page_kind::ACTION, false, action_text.title, action_text.summary);

  lv_obj_t* body = lv_obj_create(main_content);
  lv_obj_set_pos(body, ui.outer_margin, 0);
  lv_obj_set_width(body, ui.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  set_surface_style(body, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  disable_scrolling(body);
  lv_obj_set_style_pad_bottom(body, navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  int info_height = std::max(ui.card_height, ui_px(168));
  const int info_side_padding = card_inner_padding();
  lv_obj_t* info = lv_obj_create(body);
  lv_obj_set_size(info, ui.content_width, info_height);
  set_surface_style(info, ui.card_color);
  lv_obj_set_style_radius(info, info_height / 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(info, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(info, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(info, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(info, ui_px(3), LV_PART_MAIN);
  disable_scrolling(info);

  const int info_icon_size = std::min(ui.icon_size, info_height - ui_px(32));
  lv_obj_t* icon = lv_obj_create(info);
  lv_obj_set_size(icon, info_icon_size, info_icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, info_side_padding, 0);
  lv_obj_set_style_radius(icon, info_icon_size / 4, LV_PART_MAIN);
  set_surface_style(icon, lv_color_hex(definition.color));
  disable_scrolling(icon);

  const int info_icon_art_size = action_icon_art_size(info_icon_size);
  lv_obj_t* icon_image =
      create_svg_image(icon, definition.icon, info_icon_art_size, info_icon_art_size);
  lv_obj_center(icon_image);

  const int text_left = info_side_padding + info_icon_size + ui.cards_top_gap;
  const int text_gap = ui_px(12);
  const int text_width = std::max(1, ui.content_width - text_left - info_side_padding);

  lv_obj_t* text_block = lv_obj_create(info);
  lv_obj_set_width(text_block, text_width);
  lv_obj_set_height(text_block, LV_SIZE_CONTENT);
  set_surface_style(text_block, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(text_block, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(text_block, text_gap, LV_PART_MAIN);
  lv_obj_set_layout(text_block, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(text_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(text_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(text_block, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(text_block);

  lv_obj_t* info_title = lv_label_create(text_block);
  lv_label_set_text(info_title, action_text.summary);
  lv_label_set_long_mode(info_title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(info_title, text_width);
  lv_obj_set_style_text_color(info_title, ui.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(info_title, ui.text_font, LV_PART_MAIN);

  lv_obj_t* info_detail = lv_label_create(text_block);
  lv_label_set_text(info_detail, action_text.detail);
  lv_label_set_long_mode(info_detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(info_detail, text_width);
  lv_obj_set_style_text_color(info_detail, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(info_detail, ui.status_font, LV_PART_MAIN);

  lv_obj_update_layout(text_block);
  info_height = std::max(info_height, lv_obj_get_height(text_block) + ui_px(32));
  lv_obj_set_height(info, info_height);
  lv_obj_set_style_radius(info, info_height / 4, LV_PART_MAIN);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, info_side_padding, 0);
  lv_obj_align(text_block, LV_ALIGN_LEFT_MID, text_left, 0);
  lv_obj_update_layout(body);

  if (definition.id == action_id::SETTINGS) {
    create_setting_option(body, strings().language_title, strings().current_language_detail,
                          &language_target);
    create_setting_option(body, strings().time_title, strings().time_summary, &timezone_target);
    if (hardware != nullptr && hardware->has_brightness()) {
      create_setting_option(body, strings().brightness_title, strings().brightness_summary,
                            &brightness_target);
    }
    if (hardware != nullptr && hardware->has_haptics()) {
      create_setting_option(body, strings().haptics_title, strings().haptics_summary,
                            &haptics_target);
    }
    if (screen != nullptr && screen->has_recording()) {
      create_setting_option(body, strings().recording_settings_title,
                            strings().recording_settings_summary, &recording_target);
    }
    create_setting_option(body, strings().classic_gui_title, strings().classic_gui_detail,
                          &legacy_target);
  }
}

static lv_obj_t* create_apply_button(lv_event_cb_t callback, const char* text) {
  const int button_height = single_line_card_height();
  const int button_width = ui.content_width;
  const int button_padding = ui.cards_top_gap;
  const int page_height = ui.height - ui.status_height - ui.nav_height;

  lv_obj_t* button = lv_obj_create(page_layer);
  lv_obj_set_size(button, button_width, button_height);
  lv_obj_set_pos(button, ui.outer_margin, page_height - button_height - button_padding);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(button, button_height / 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x347FF1), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_mix(lv_color_hex(0xFFFFFF), lv_color_hex(0x347FF1), 18), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(button, ui_px(3), LV_PART_MAIN);
  disable_scrolling(button);
  add_press_cancel_guard(button);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, ui.text_font, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

static void show_language_page(void) {
  pending_language = current_language;

  const int button_height = single_line_card_height();
  const int bottom_reserved = button_height + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::LANGUAGE, false, strings().language_title,
                       strings().language_summary, bottom_reserved);

  lv_obj_t* body = lv_obj_create(main_content);
  lv_obj_set_pos(body, ui.outer_margin, 0);
  lv_obj_set_width(body, ui.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  set_surface_style(body, ui.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  disable_scrolling(body);
  lv_obj_set_style_pad_bottom(body, navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  for (int i = 0; i < 3; ++i) create_language_option(body, i, language_values[i]);

  create_apply_button(apply_language_event_cb, strings().apply);
}

static void layout_status_labels(void) {
  if (status_time_label == nullptr || runtime_status_font == nullptr) return;

  const int status_content_height = ui.status_height - ui.status_top_padding;
  const int status_y =
      ui.status_top_padding + (status_content_height - runtime_status_font->line_height) / 2;
  lv_obj_align(status_time_label, LV_ALIGN_TOP_LEFT, ui.outer_margin, status_y);
  if (recording_indicator != nullptr)
    lv_obj_align_to(recording_indicator, status_time_label, LV_ALIGN_OUT_RIGHT_MID, ui_px(12), 0);
}

static void refresh_status_bar(lv_timer_t* timer) {
  auto* provider = static_cast<gui2_backend::status_backend*>(lv_timer_get_user_data(timer));
  if (provider == nullptr) return;
  const gui2_backend::status_snapshot snapshot = provider->snapshot();

  if (status_time_label != nullptr)
    lv_label_set_text(status_time_label, snapshot.time_text.c_str());
  refresh_recording_ui();

  if (battery_value_label == nullptr || battery_icon == nullptr) return;
  if (!snapshot.battery_valid) {
    lv_obj_add_flag(battery_value_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(battery_icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char battery_text[16];
  std::snprintf(battery_text, sizeof(battery_text), "%d%%",
                std::clamp(snapshot.battery_percentage, 0, 100));
  lv_label_set_text(battery_value_label, battery_text);
  lv_image_set_src(
      battery_icon,
      gui2_svg_get_raster(battery_icon_for(snapshot.battery_percentage, snapshot.charging),
                          lv_obj_get_width(battery_icon), lv_obj_get_height(battery_icon)));

  const int status_content_height = ui.status_height - ui.status_top_padding;
  const int status_y =
      ui.status_top_padding + (status_content_height - runtime_status_font->line_height) / 2;
  lv_obj_align(battery_value_label, LV_ALIGN_TOP_RIGHT, -ui.outer_margin, status_y);
  lv_obj_align_to(battery_icon, battery_value_label, LV_ALIGN_OUT_LEFT_MID, -ui_px(12), 0);
  lv_obj_clear_flag(battery_value_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(battery_icon, LV_OBJ_FLAG_HIDDEN);
}

static void create_gui2_shell(lv_obj_t* screen) {
  const int width = gr_fb_width();
  const int height = gr_fb_height();
  const bool dark_mode = true;
  const bool landscape = width > height;

  const lv_color_t background = lv_color_hex(dark_mode ? 0x000000 : 0xF7F7F7);
  const lv_color_t card_color = lv_color_hex(dark_mode ? 0x252525 : 0xFFFFFF);
  const lv_color_t nav_color = lv_color_hex(dark_mode ? 0x252525 : 0xFFFFFF);
  const lv_color_t primary_text = lv_color_hex(dark_mode ? 0xFFFFFF : 0x121212);
  const lv_color_t secondary_text = lv_color_hex(dark_mode ? 0x898989 : 0x777777);
  const lv_font_t* text_font = ui_text_font();

  const float scale = ui_scale_for(width, height);
  ui.scale = scale;
  const int status_content_height = std::clamp(ui_px(96), ui_px(56), ui_px(96));
  const int status_top_padding = std::clamp(ui_px(24), ui_px(12), ui_px(24));
  const int status_height = status_content_height + status_top_padding;
  const int nav_height = std::clamp(ui_px(210), ui_px(154), ui_px(210));
  const int outer_margin = std::clamp(ui_px(56), ui_px(18), ui_px(56));
  const int card_gap = std::clamp(ui_px(22), ui_px(10), ui_px(22));
  const int content_width = width - outer_margin * 2;
  const int card_height = landscape ? std::clamp(height * 12 / 100, ui_px(82), ui_px(148))
                                    : std::clamp(width * 18 / 100, ui_px(104), ui_px(210));
  const int icon_size = std::clamp(card_height * 58 / 100, ui_px(48), ui_px(112));
  const lv_font_t* brand_font = runtime_brand_font;

  ui = {
    width,
    height,
    scale,
    status_height,
    status_top_padding,
    nav_height,
    outer_margin,
    card_gap,
    content_width,
    std::clamp(ui_px(112), ui_px(72), ui_px(112)),
    std::clamp(ui_px(24), ui_px(16), ui_px(24)),
    std::clamp(brand_font->line_height + runtime_status_font->line_height + ui_px(18), ui_px(120),
               ui_px(180)),
    card_height,
    icon_size,
    text_font,
    runtime_status_font,
    brand_font,
    background,
    card_color,
    nav_color,
    primary_text,
    secondary_text,
  };

  lv_obj_set_style_bg_color(screen, background, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_font(screen, text_font, LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  disable_scrolling(screen);

  lv_obj_t* status_bar = lv_obj_create(screen);
  lv_obj_set_pos(status_bar, 0, 0);
  lv_obj_set_size(status_bar, width, status_height);
  set_surface_style(status_bar, background);
  lv_obj_set_style_pad_all(status_bar, 0, LV_PART_MAIN);
  lv_obj_add_flag(status_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(status_bar, status_gesture_event_cb, LV_EVENT_ALL, nullptr);
  disable_scrolling(status_bar);

  status_time_label = lv_label_create(status_bar);
  lv_label_set_text(status_time_label, "");
  lv_obj_align(status_time_label, LV_ALIGN_TOP_LEFT, outer_margin,
               status_top_padding + (status_content_height - runtime_status_font->line_height) / 2);
  lv_obj_set_style_text_color(status_time_label, primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(status_time_label, runtime_status_font, LV_PART_MAIN);

  battery_value_label = lv_label_create(status_bar);
  lv_label_set_text(battery_value_label, "--%");
  lv_obj_set_style_text_color(battery_value_label, primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(battery_value_label, runtime_status_font, LV_PART_MAIN);
  lv_obj_align(battery_value_label, LV_ALIGN_TOP_RIGHT, -outer_margin,
               status_top_padding + (status_content_height - runtime_status_font->line_height) / 2);

  battery_icon = create_svg_image(status_bar, &kGui2IconBattery100, ui_px(72), ui_px(48));
  lv_obj_align_to(battery_icon, battery_value_label, LV_ALIGN_OUT_LEFT_MID, -ui_px(12), 0);

  recording_indicator = lv_label_create(status_bar);
  lv_label_set_text(recording_indicator, strings().recording_indicator);
  lv_obj_align_to(recording_indicator, status_time_label, LV_ALIGN_OUT_RIGHT_MID, ui_px(12), 0);
  lv_obj_set_style_text_color(recording_indicator, lv_color_hex(0xF0443E), LV_PART_MAIN);
  lv_obj_set_style_text_font(recording_indicator, runtime_status_font, LV_PART_MAIN);
  lv_obj_add_flag(recording_indicator, LV_OBJ_FLAG_HIDDEN);

  page_layer = lv_obj_create(screen);
  lv_obj_set_pos(page_layer, 0, status_height);
  lv_obj_set_size(page_layer, width, std::max(1, height - status_height));
  set_surface_style(page_layer, background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(page_layer, 0, LV_PART_MAIN);
  disable_scrolling(page_layer);

  show_home_page();

  lv_obj_t* navigation = lv_obj_create(screen);
  lv_obj_set_pos(navigation, 0, height - nav_height);
  lv_obj_set_size(navigation, width, nav_height);
  set_surface_style(navigation, background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(navigation, 0, LV_PART_MAIN);
  disable_scrolling(navigation);

  const int navigation_control_size = std::clamp(nav_height * 72 / 100, ui_px(108), ui_px(148));
  const int nav_button_size = navigation_control_size;
  const int pill_height = navigation_control_size;
  const int pill_width =
      std::clamp(landscape ? width * 44 / 100 : width * 64 / 100, ui_px(270), ui_px(700));
  const int navigation_gap = std::clamp(ui_px(30), ui_px(18), ui_px(30));
  const int navigation_group_width = nav_button_size + navigation_gap + pill_width;
  const int navigation_group_left = std::max(0, (width - navigation_group_width) / 2);
  const int navigation_bottom_padding = std::clamp(ui_px(22), ui_px(12), ui_px(22));
  const int navigation_content_height = std::max(1, nav_height - navigation_bottom_padding);
  const int navigation_group_top = std::max(0, (navigation_content_height - nav_button_size) / 2);
  const int pill_top = std::max(0, (navigation_content_height - pill_height) / 2);

  lv_obj_t* back = create_nav_button(navigation, &kGui2IconBack, nav_button_size, true, nav_color,
                                     primary_text, back_navigation);
  lv_obj_set_pos(back, navigation_group_left, navigation_group_top);

  lv_obj_t* pill = lv_obj_create(navigation);
  lv_obj_set_size(pill, pill_width, pill_height);
  lv_obj_set_pos(pill, navigation_group_left + nav_button_size + navigation_gap, pill_top);
  set_surface_style(pill, nav_color);
  lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(pill, pill_height / 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(pill, ui_px(4), LV_PART_MAIN);
  lv_obj_set_style_border_color(pill, lv_color_hex(0x3B3B3B), LV_PART_MAIN);
  lv_obj_set_style_border_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(pill, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(pill, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(pill, ui_px(3), LV_PART_MAIN);
  lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 0, LV_PART_MAIN);
  disable_scrolling(pill);

  const int pill_item_width = pill_width / 3;
  const int pill_icon_size = std::min(pill_height - ui_px(12), ui_px(108));
  lv_obj_t* home_button = create_nav_button(
      pill, home_navigation_active ? &kGui2IconHomeFilled : &kGui2IconHomeOutlined, pill_icon_size,
      false, nav_color, primary_text, home_navigation, pill_item_width, pill_height, true);
  lv_obj_t* console_button =
      create_nav_button(pill, &kGui2IconConsoleOutline, pill_icon_size, false, nav_color,
                        secondary_text, log_navigation, pill_item_width, pill_height, true);
  lv_obj_t* power_button =
      create_nav_button(pill, &kGui2IconPower, pill_icon_size, false, nav_color, secondary_text,
                        power_navigation, pill_width - pill_item_width * 2, pill_height, true);
  nav_home_icon = home_button == nullptr ? nullptr : lv_obj_get_child(home_button, 0);
  nav_console_icon = console_button == nullptr ? nullptr : lv_obj_get_child(console_button, 0);
  nav_power_icon = power_button == nullptr ? nullptr : lv_obj_get_child(power_button, 0);
  refresh_navigation_icons();

  create_quick_menu();
  create_mouse_cursor();
}

static void shutdown_gui2(bool keep_display = false) {
  if (screen != nullptr && screen->is_recording()) screen->stop_recording();
  if (status_provider != nullptr) {
    status_provider->stop();
    status_provider.reset();
  }

  lv_deinit();
  gui2_svg_cache_clear();
  gui2_display_deinit();
  if (runtime_text_font != nullptr) lv_tiny_ttf_destroy(runtime_text_font);
  if (runtime_status_font != nullptr) lv_tiny_ttf_destroy(runtime_status_font);
  if (runtime_brand_font != nullptr) lv_tiny_ttf_destroy(runtime_brand_font);
  runtime_text_font = nullptr;
  runtime_status_font = nullptr;
  runtime_brand_font = nullptr;
  pointer_indev = nullptr;
  status_timer = nullptr;
  battery_icon = nullptr;
  status_time_label = nullptr;
  battery_value_label = nullptr;
  recording_indicator = nullptr;
  mouse_cursor = nullptr;
  quick_menu = nullptr;
  quick_dismiss = nullptr;
  quick_record_button = nullptr;
  quick_record_label = nullptr;
  quick_feedback = nullptr;
  quick_brightness_binding = {};
  quick_brightness_dirty = false;
  screenshot_flash = nullptr;
  screenshot_flash_until_ms = 0;
  pending_screenshot = false;
  pending_screen_off = false;
  reset_wheel_scroll();
  screen = nullptr;
  ev_exit();
  if (!keep_display) gr_exit();
}

int gui2_start(const gui2_context* context) {
  if (context == nullptr || context->settings == nullptr || context->hardware == nullptr ||
      context->screen == nullptr)
    return GUI2_EXIT_INITIALIZATION_FAILED;

  settings = context->settings;
  hardware = context->hardware;
  screen = context->screen;
  current_language = language_from_code(settings->get_string("tw_language", "en"));
  pending_language = current_language;
  switch_to_legacy = false;
  pending_screenshot = false;
  pending_screen_off = false;
  screenshot_flash_until_ms = 0;

  if (!context->display_initialized && gr_init() < 0) return GUI2_EXIT_INITIALIZATION_FAILED;

  ev_init();
  lv_init();
  lv_tick_set_cb(lv_tick_ms);
  init_ui_font();
  if (runtime_text_font == nullptr || runtime_status_font == nullptr ||
      runtime_brand_font == nullptr) {
    shutdown_gui2();
    return GUI2_EXIT_INITIALIZATION_FAILED;
  }

  lv_display_t* display = gui2_display_init();
  if (!display) {
    shutdown_gui2();
    return GUI2_EXIT_INITIALIZATION_FAILED;
  }

  pointer_indev = gui2_input_init();
  if (!pointer_indev) {
    shutdown_gui2();
    return GUI2_EXIT_INITIALIZATION_FAILED;
  }

  lv_timer_set_period(lv_indev_get_read_timer(pointer_indev), 5);

#if LV_USE_GESTURE_RECOGNITION
  lv_indev_set_pinch_up_threshold(pointer_indev, 1.20f);
  lv_indev_set_pinch_down_threshold(pointer_indev, 0.80f);
#endif

  create_gui2_shell(lv_screen_active());

  status_provider = std::make_unique<gui2_backend::status_backend>(settings);
  status_provider->start();
  status_timer = lv_timer_create(refresh_status_bar, 1000, status_provider.get());
  if (status_timer != nullptr) refresh_status_bar(status_timer);

  auto& perf_manager = twrp::TwrpPerfManager::Get();
  perf_manager.Initialize();

  for (;;) {
    if (switch_to_legacy) break;
    const uint64_t loop_start_ms = monotonic_ms();
    advance_wheel_scroll(loop_start_ms);
    perf_manager.Update();
    screen->tick(loop_start_ms);
    gui2_input_set_screen_off(screen->is_screen_off());
    update_screenshot_flash(loop_start_ms);
    uint32_t delay_ms = lv_timer_handler();

    gui2_key_action key_action;
    while (gui2_input_take_key_action(&key_action)) {
      if (key_action == gui2_key_action::SCREENSHOT) {
        close_quick_menu();
        pending_screenshot = true;
      } else if (key_action == gui2_key_action::BACK) {
        navigate_back();
      } else if (key_action == gui2_key_action::TOGGLE_SCREEN) {
        if (screen->is_screen_off()) {
          if (screen->screen_on()) {
            gui2_input_set_screen_off(false);
            lv_obj_invalidate(lv_screen_active());
          }
        } else {
          pending_screen_off = true;
        }
      }
    }

    if (gui2_input_take_activity()) {
      const bool was_screen_off = screen->is_screen_off();
      screen->on_input_activity();
      if (was_screen_off) {
        gui2_input_set_screen_off(false);
        lv_obj_invalidate(lv_screen_active());
      }
      perf_manager.NotifyInteraction();
    }
    const int wheel = gui2_input_take_wheel();
    if (wheel != 0 && main_content != nullptr && !quick_menu_input_active()) {
      lv_point_t point;
      lv_indev_get_point(pointer_indev, &point);
      lv_area_t content_area;
      lv_obj_get_coords(main_content, &content_area);

      const bool over_content = point.x >= content_area.x1 && point.x <= content_area.x2 &&
                                point.y >= content_area.y1 && point.y <= content_area.y2;
      if (over_content) {
        queue_wheel_scroll(wheel);
      }
    }

    if (gui2_display_present(screen, loop_start_ms)) perf_manager.NotifyFrameActivity();
    process_pending_screen_actions();
    delay_ms = perf_manager.ClampTimeoutMs(delay_ms);

    if (delay_ms == LV_NO_TIMER_READY || delay_ms > 50) delay_ms = 10;
    if (delay_ms == 0) delay_ms = 1;

    const uint64_t elapsed_ms = monotonic_ms() - loop_start_ms;
    if (delay_ms > elapsed_ms) usleep((delay_ms - elapsed_ms) * 1000);
  }

  shutdown_gui2(switch_to_legacy);
  return switch_to_legacy ? GUI2_EXIT_TO_LEGACY : 0;
}
