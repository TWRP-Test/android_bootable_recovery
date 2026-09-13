#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "backend/hardware_settings.h"
#include "backend/status_backend.h"
#include "gui2.h"
#include "gui2_display.h"
#include "gui2_input.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include "twrpminui/minui.h"
#include "twrpperf/perf_manager.hpp"

// The WQY font is loaded from TWRP's theme resources at runtime.

static lv_font_t* runtime_text_font;
static lv_font_t* runtime_status_font;
static lv_font_t* runtime_brand_font;
static gui2_backend::settings_store* settings;
static gui2_backend::hardware_settings* hardware;
static std::unique_ptr<gui2_backend::status_backend> status_provider;
static lv_timer_t* status_timer;

static const lv_font_t* ui_text_font(void) {
  return runtime_text_font;
}

static void init_ui_font(void) {
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  // Theme resources are installed below /twres in a recovery image. WQY
  // is a TTC collection; tiny_ttf selects its first face.
  // Scale the typography with the display.  The previous fixed 32px font
  // was readable on a small panel but looked undersized on the 1080px
  // emux64 phone display.
  const int unit = std::max(1, std::min(gr_fb_width(), gr_fb_height()) / 100);
  const int text_size = std::clamp(unit * 5, 36, 52);
  const int status_size = std::clamp(unit * 3, 26, 36);
  const int brand_size = std::clamp(unit * 9, 72, 96);
  runtime_text_font = lv_tiny_ttf_create_file("/twres/fonts/wqy-microhei.ttf", text_size);
  runtime_status_font = lv_tiny_ttf_create_file("/twres/fonts/wqy-microhei.ttf", status_size);
  runtime_brand_font = lv_tiny_ttf_create_file("/twres/fonts/wqy-microhei.ttf", brand_size);
#endif
}

static uint32_t monotonic_ms(void) {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static lv_indev_t* pointer_indev;
static lv_obj_t* main_content;
static lv_obj_t* status_hint;
static lv_obj_t* page_layer;
static lv_obj_t* heading;
static lv_obj_t* status_time_label;
static lv_obj_t* battery_value_label;
static lv_obj_t* battery_icon;
static lv_obj_t* battery_charge_icon;
static lv_obj_t* legacy_dialog;
static bool home_page_active;
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

struct ui_metrics {
  int width;
  int height;
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
  const char* symbol;
  uint32_t color;
};

static const action_definition actions[] = {
  { action_id::INSTALL, LV_SYMBOL_DOWNLOAD, 0x347FF1 },
  { action_id::WIPE, LV_SYMBOL_CUT, 0xF0443E },
  { action_id::BACKUP, LV_SYMBOL_SAVE, 0xFFAA20 },
  { action_id::RESTORE, LV_SYMBOL_UPLOAD, 0x18C935 },
  { action_id::MOUNT, LV_SYMBOL_DRIVE, 0x6754E8 },
  { action_id::ADVANCED, LV_SYMBOL_BARS, 0x9AA7B0 },
  { action_id::SETTINGS, LV_SYMBOL_SETTINGS, 0x405A6C },
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
static void create_gui2_shell(lv_obj_t* screen);
static lv_obj_t* create_apply_button(lv_event_cb_t callback, const char* text);
static int card_inner_padding(void);

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
};

static hardware_slider_binding brightness_binding;
static hardware_slider_binding haptic_bindings[3];

static void refresh_status_bar(lv_timer_t* timer);

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

static void press_cancel_guard_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));

  if (code == LV_EVENT_PRESSED) {
    click_target = target;
    click_cancelled = false;
  } else if (code == LV_EVENT_PRESSING && click_target == target && pointer_indev != nullptr) {
    // Some pages have no overflowing content, so LVGL may keep the
    // original active object instead of producing PRESS_LOST while the
    // pointer is being dragged. Detect leaving the hit area directly.
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
  LEGACY,
};

static constexpr settings_target language_target = settings_target::LANGUAGE;
static constexpr settings_target timezone_target = settings_target::TIMEZONE;
static constexpr settings_target brightness_target = settings_target::BRIGHTNESS;
static constexpr settings_target haptics_target = settings_target::HAPTICS;
static constexpr settings_target legacy_target = settings_target::LEGACY;

static void navigation_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  const auto* action = static_cast<const navigation_action*>(lv_event_get_user_data(event));
  if (action == nullptr) return;

  if (*action == navigation_action::BACK || *action == navigation_action::HOME) {
    // BACK returns one level from a feature page; HOME is intentionally
    // unconditional so it also works as a direct escape from a sub-page.
    if (*action == navigation_action::HOME) {
      show_home_page();
    } else if (current_page == page_kind::LANGUAGE || current_page == page_kind::TIMEZONE ||
               current_page == page_kind::BRIGHTNESS || current_page == page_kind::HAPTICS) {
      show_action_page(actions[static_cast<int>(action_id::SETTINGS)]);
    } else if (!home_page_active) {
      show_home_page();
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
  // The settings card has already passed accept_click() before routing here.
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  const int dialog_width = std::min(ui.content_width, 720);
  const int dialog_height = std::min(ui.height - ui.status_height - ui.nav_height, 360);
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
  lv_obj_set_style_radius(card, dialog_height / 8, LV_PART_MAIN);
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
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, ui.text_font->line_height + 20);
  lv_obj_set_style_text_color(body, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(body, ui.status_font, LV_PART_MAIN);

  lv_obj_t* cancel = lv_obj_create(card);
  lv_obj_set_size(cancel, (dialog_width - card_inner_padding() * 2 - ui.card_gap) / 2, 82);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  set_surface_style(cancel, ui.background);
  lv_obj_set_style_radius(cancel, 24, LV_PART_MAIN);
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
  lv_obj_set_size(confirm, (dialog_width - card_inner_padding() * 2 - ui.card_gap) / 2, 82);
  lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  set_surface_style(confirm, lv_color_hex(0x347FF1));
  lv_obj_set_style_radius(confirm, 24, LV_PART_MAIN);
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

static int card_inner_padding(void) {
  // Use one responsive horizontal inset for every card-like component.
  // The clamp keeps the inset usable on small displays while preventing
  // wide phone cards from becoming too tightly padded.
  return std::clamp(ui.outer_margin, 32, 56);
}

// All single-line choices use the same vertical rhythm as the timezone
// choices. Keeping this in one place prevents compact rows and action
// buttons from becoming visibly narrower than their neighboring cards.
static int single_line_card_height() {
  return std::clamp(ui.card_height * 2 / 3, 96, 148);
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
  lv_obj_set_style_shadow_width(card, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, 3, LV_PART_MAIN);
  add_press_cancel_guard(card);
  lv_obj_add_event_cb(card, action_card_event_cb, LV_EVENT_CLICKED,
                      const_cast<action_definition*>(&definition));

  const int card_side_padding = card_inner_padding();
  const int title_gap = std::clamp(card_height / 8, 20, 28);

  lv_obj_t* icon = lv_obj_create(card);
  lv_obj_set_size(icon, icon_size, icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, card_side_padding, 0);
  lv_obj_set_style_radius(icon, icon_size / 4, LV_PART_MAIN);
  set_surface_style(icon, lv_color_hex(definition.color));
  // lv_obj_create() is clickable by default. The icon is decorative, so it
  // must not intercept the card's pointer events.
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  disable_scrolling(icon);

  lv_obj_t* icon_label = lv_label_create(icon);
  lv_label_set_text(icon_label, definition.symbol);
  lv_obj_set_style_text_color(icon_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  // The 24px default font made the glyphs look lost inside the enlarged
  // touch targets.  Montserrat 48 contains the FontAwesome symbols used by
  // the action cards and is independent of the CJK TinyTTF font.
  lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_center(icon_label);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, strings().actions[static_cast<int>(definition.id)].title);
  lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  const int title_left = card_side_padding + icon_size + title_gap;
  const int title_width = std::max(1, card_width - title_left - card_side_padding - 36);
  lv_obj_set_width(title, title_width);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, title_left, 0);
  lv_obj_set_style_text_color(title, text_color, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, card_font, LV_PART_MAIN);

  lv_obj_t* arrow = lv_label_create(card);
  lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -card_side_padding, 0);
  lv_obj_set_style_text_color(arrow, secondary_color, LV_PART_MAIN);
  lv_obj_set_style_text_font(arrow, &lv_font_montserrat_48, LV_PART_MAIN);

  return card;
}

static lv_obj_t* create_nav_button(lv_obj_t* parent, const char* symbol, int size, bool circular,
                                   lv_color_t nav_color, lv_color_t text_color,
                                   const navigation_action& action, int width = -1,
                                   int height = -1) {
  lv_obj_t* button = lv_obj_create(parent);
  const int button_width = width > 0 ? width : size;
  const int button_height = height > 0 ? height : size;
  lv_obj_set_size(button, button_width, button_height);
  disable_scrolling(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  const int radius_size = std::min(button_width, button_height);
  lv_obj_set_style_radius(button, circular ? radius_size / 2 : radius_size / 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, nav_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), nav_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  add_press_cancel_guard(button);
  lv_obj_add_event_cb(button, navigation_event_cb, LV_EVENT_CLICKED,
                      const_cast<navigation_action*>(&action));

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, symbol);
  lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_center(label);
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
  lv_obj_set_style_pad_left(heading, 10, LV_PART_MAIN);
  disable_scrolling(heading);

  lv_obj_t* title_label = lv_label_create(heading);
  lv_label_set_text(title_label, title);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 2);
  lv_obj_set_style_text_color(title_label, ui.primary_text, LV_PART_MAIN);
  // All page titles use the same shared brand font. Keeping this decision
  // in the shell prevents secondary and tertiary pages from drifting away
  // when the title size is adjusted.
  lv_obj_set_style_text_font(title_label, ui.brand_font, LV_PART_MAIN);

  lv_obj_t* version = lv_label_create(heading);
  lv_label_set_text(version, "4.0.0");
  lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -4, 10);
  lv_obj_set_style_text_color(version, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(version, ui.status_font, LV_PART_MAIN);

  status_hint = lv_label_create(heading);
  lv_label_set_text(status_hint, summary);
  lv_obj_align(status_hint, LV_ALIGN_BOTTOM_LEFT, 0, -6);
  lv_obj_set_style_text_color(status_hint, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(status_hint, ui.status_font, LV_PART_MAIN);
}

static void create_scroll_area(int bottom_reserved = 0) {
  const int scroll_top = ui.heading_top + ui.heading_height + ui.cards_top_gap;

  main_content = lv_obj_create(page_layer);
  lv_obj_set_pos(main_content, 0, scroll_top);
  lv_obj_set_size(
      main_content, ui.width,
      std::max(1, ui.height - ui.status_height - ui.nav_height - scroll_top - bottom_reserved));
  set_surface_style(main_content, ui.background);
  lv_obj_add_flag(main_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(main_content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(main_content, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(main_content, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_style_pad_all(main_content, 0, LV_PART_MAIN);
}

static void create_page_scaffold(page_kind page, bool is_home, const char* title,
                                 const char* summary, int bottom_reserved = 0) {
  reset_page_layer();
  home_page_active = is_home;
  current_page = page;
  create_page_heading(title, summary);
  create_scroll_area(bottom_reserved);
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
  else
    request_legacy_gui_event_cb(event);
}

static lv_obj_t* create_setting_option(lv_obj_t* parent, const char* title, const char* detail,
                                       const settings_target* target) {
  int option_height = std::max(ui.card_height, 132);
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
  lv_obj_set_style_shadow_width(option, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(option, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(option, 3, LV_PART_MAIN);
  disable_scrolling(option);
  add_press_cancel_guard(option);
  lv_obj_add_event_cb(option, settings_option_event_cb, LV_EVENT_CLICKED,
                      const_cast<settings_target*>(target));

  const int text_left = card_inner_padding();
  const int text_gap = 8;
  const int text_right = card_inner_padding() + 56;
  const int text_width = std::max(1, ui.content_width - text_left - text_right);

  // The complete text group, rather than each label, is centered. Its
  // height is determined after wrapping, so one-, two- and multi-line
  // descriptions all remain vertically balanced.
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
  option_height = std::max(option_height, lv_obj_get_height(text_block) + 32);
  lv_obj_set_height(option, option_height);
  lv_obj_align(text_block, LV_ALIGN_LEFT_MID, text_left, 0);

  lv_obj_t* arrow = lv_label_create(option);
  lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -card_inner_padding(), 0);
  lv_obj_set_style_text_color(arrow, ui.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(arrow, &lv_font_montserrat_48, LV_PART_MAIN);

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
  const int option_height = std::max(ui.card_height, 118);
  lv_obj_t* option = lv_obj_create(parent);
  lv_obj_set_size(option, ui.content_width, option_height);
  lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(option, option_height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(option, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(option, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(option, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(option, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(option, 3, LV_PART_MAIN);
  disable_scrolling(option);
  add_press_cancel_guard(option);
  // The language values are compile-time constants, so their addresses are
  // stable event data for the lifetime of the GUI.
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
  lv_obj_set_style_shadow_width(card, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, 3, LV_PART_MAIN);
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
  if (binding.brightness)
    lv_label_set_text_fmt(binding.value_label, "%d%%", value);
  else
    lv_label_set_text_fmt(binding.value_label, "%d ms", value);
}

static void hardware_slider_event_cb(lv_event_t* event) {
  auto* binding = static_cast<hardware_slider_binding*>(lv_event_get_user_data(event));
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (binding == nullptr || slider == nullptr || hardware == nullptr) return;

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    const int value = lv_slider_get_value(slider);
    const bool applied = binding->brightness
                             ? hardware->set_brightness_percent(value)
                             : hardware->set_haptic_duration_ms(binding->channel, value);
    if (!applied) {
      show_hardware_error(strings().hardware_error);
      return;
    }
    hardware_settings_dirty = true;
    update_hardware_slider_value(*binding, value);
    clear_hardware_error();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
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
  // Keep the card's content independent from LVGL's font metrics. In
  // particular, use explicit geometry for the top label row and the
  // bottom slider instead of competing alignment anchors: a TinyTTF label
  // can have a larger line box than its visible glyphs, which makes the two
  // areas overlap on device-sized fonts.
  const int side_padding = card_inner_padding();
  const int header_height = std::max(1, ui.text_font->line_height);
  const int content_gap = std::clamp(ui.card_gap, 12, 18);
  const int slider_height = 28;
  const int content_height = header_height + content_gap + slider_height;
  const int card_height = std::max(ui.card_height, side_padding * 2 + content_height);
  const int card_width = ui.content_width;
  const int content_width = std::max(1, card_width - side_padding * 2);
  // Treat the title/value row and slider as one content group. The group is
  // centered in the card's inner rectangle, so all four outer insets stay
  // visually symmetric instead of independently anchoring each widget.
  const int inner_height = std::max(1, card_height - side_padding * 2);
  const int content_top = side_padding + std::max(0, (inner_height - content_height) / 2);
  const int slider_top = content_top + header_height + content_gap;

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, card_width, card_height);
  set_surface_style(card, ui.card_color);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, 3, LV_PART_MAIN);
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

  lv_obj_t* slider = lv_slider_create(card);
  lv_obj_set_width(slider, content_width);
  lv_obj_set_height(slider, slider_height);
  // Anchor the slider to the card's lower content edge. This keeps the
  // title row at the top while making the bottom inset match the card's
  // horizontal padding on every display size.
  lv_obj_set_pos(slider, side_padding, card_height - slider_height - side_padding);
  lv_slider_set_range(slider, minimum, maximum);
  lv_slider_set_value(slider, std::clamp(value, minimum, maximum), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x347FF1), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, 12, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
  lv_obj_add_event_cb(slider, hardware_slider_event_cb, LV_EVENT_VALUE_CHANGED, binding);
  lv_obj_add_event_cb(slider, hardware_slider_event_cb, LV_EVENT_RELEASED, binding);
  lv_obj_add_event_cb(slider, hardware_slider_event_cb, LV_EVENT_PRESS_LOST, binding);
  update_hardware_slider_value(*binding, lv_slider_get_value(slider));
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
  lv_obj_set_style_pad_top(body, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(body, ui.outer_margin, LV_PART_MAIN);
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
  brightness_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, true };
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
    binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false };
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
  lv_obj_set_style_pad_bottom(body, ui.outer_margin, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  create_section_label(body, strings().time_format);
  lv_obj_t* format_row = lv_obj_create(body);
  const int choice_height = single_line_card_height();
  const int row_padding = 10;
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
  // Leave room around both flex rows for the cards' rounded corners and
  // shadows. Without this, the row container clips the outer edges.
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
  lv_obj_set_style_pad_bottom(cards, ui.outer_margin, LV_PART_MAIN);
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
  lv_obj_set_style_pad_bottom(body, ui.outer_margin, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  int info_height = std::max(ui.card_height, 168);
  const int info_side_padding = card_inner_padding();
  lv_obj_t* info = lv_obj_create(body);
  lv_obj_set_size(info, ui.content_width, info_height);
  set_surface_style(info, ui.card_color);
  lv_obj_set_style_radius(info, info_height / 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(info, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(info, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(info, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(info, 3, LV_PART_MAIN);
  disable_scrolling(info);

  const int info_icon_size = std::min(ui.icon_size, info_height - 32);
  lv_obj_t* icon = lv_obj_create(info);
  lv_obj_set_size(icon, info_icon_size, info_icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, info_side_padding, 0);
  lv_obj_set_style_radius(icon, info_icon_size / 4, LV_PART_MAIN);
  set_surface_style(icon, lv_color_hex(definition.color));
  disable_scrolling(icon);

  lv_obj_t* icon_label = lv_label_create(icon);
  lv_label_set_text(icon_label, definition.symbol);
  lv_obj_set_style_text_color(icon_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_center(icon_label);

  const int text_left = info_side_padding + info_icon_size + ui.cards_top_gap;
  const int text_gap = 12;
  const int text_width = std::max(1, ui.content_width - text_left - info_side_padding);

  // Measure and center the complete title/description group. The old
  // implementation assumed a one-line description, which made a wrapped
  // second or third line drift outside the visual center of the card.
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
  info_height = std::max(info_height, lv_obj_get_height(text_block) + 32);
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
  // Match the content area's horizontal bounds and use the same compact
  // vertical spacing above and below the fixed action button.
  lv_obj_set_pos(button, ui.outer_margin, page_height - button_height - button_padding);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(button, button_height / 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x347FF1), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_mix(lv_color_hex(0xFFFFFF), lv_color_hex(0x347FF1), 18), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(button, 3, LV_PART_MAIN);
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
  lv_obj_set_style_pad_bottom(body, ui.outer_margin, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  for (int i = 0; i < 3; ++i) create_language_option(body, i, language_values[i]);

  create_apply_button(apply_language_event_cb, strings().apply);
}

static const char* battery_level_symbol(int percentage) {
  if (percentage >= 85) return LV_SYMBOL_BATTERY_FULL;
  if (percentage >= 60) return LV_SYMBOL_BATTERY_3;
  if (percentage >= 30) return LV_SYMBOL_BATTERY_2;
  if (percentage > 0) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static void refresh_status_bar(lv_timer_t* timer) {
  auto* provider = static_cast<gui2_backend::status_backend*>(lv_timer_get_user_data(timer));
  if (provider == nullptr) return;
  const gui2_backend::status_snapshot snapshot = provider->snapshot();

  if (status_time_label != nullptr)
    lv_label_set_text(status_time_label, snapshot.time_text.c_str());

  if (battery_value_label == nullptr || battery_icon == nullptr || battery_charge_icon == nullptr)
    return;
  if (!snapshot.battery_valid) {
    lv_obj_add_flag(battery_value_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(battery_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char battery_text[16];
  std::snprintf(battery_text, sizeof(battery_text), "%d%%",
                std::clamp(snapshot.battery_percentage, 0, 100));
  lv_label_set_text(battery_value_label, battery_text);
  lv_label_set_text(battery_icon, battery_level_symbol(snapshot.battery_percentage));
  // Keep charging separate from the battery glyph.  Combining both symbols
  // into one label changes its width after alignment and can cover the
  // percentage label on narrow status bars.
  if (snapshot.charging) {
    lv_label_set_text(battery_charge_icon, LV_SYMBOL_CHARGE);
    lv_obj_clear_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);
  }

  // Re-anchor the complete right-hand group after every update.  Label
  // widths vary with both the percentage and the selected font, so static
  // coordinates are not sufficient here.
  const int status_content_height = ui.status_height - ui.status_top_padding;
  const int status_y =
      ui.status_top_padding + (status_content_height - runtime_status_font->line_height) / 2;
  if (snapshot.charging) {
    // The charging mark follows the percentage: battery, value, charge.
    lv_obj_align(battery_charge_icon, LV_ALIGN_TOP_RIGHT, -ui.outer_margin, status_y);
    lv_obj_align_to(battery_value_label, battery_charge_icon, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    lv_obj_align_to(battery_icon, battery_value_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  } else {
    lv_obj_align(battery_value_label, LV_ALIGN_TOP_RIGHT, -ui.outer_margin, status_y);
    lv_obj_align_to(battery_icon, battery_value_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  }
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

  const int unit = std::max(1, std::min(width, height) / 100);
  // Reserve an additional top-safe area for status text.  Modern displays
  // can have rounded corners or a camera cutout close to the top edge.
  const int status_content_height = std::clamp(unit * 8, 56, 96);
  const int status_top_padding = std::clamp(unit * 2, 12, 24);
  const int status_height = status_content_height + status_top_padding;
  const int nav_height = std::clamp(unit * 22, 180, 240);
  const int outer_margin = std::clamp(unit * 5, 18, 56);
  const int card_gap = std::clamp(unit * 2, 10, 22);
  const int content_width = width - outer_margin * 2;
  const int card_height =
      landscape ? std::clamp(height * 12 / 100, 82, 148) : std::clamp(width * 19 / 100, 104, 218);
  const int icon_size = std::clamp(card_height * 60 / 100, 64, 132);
  const lv_font_t* brand_font = runtime_brand_font;

  ui = {
    width,
    height,
    status_height,
    status_top_padding,
    nav_height,
    outer_margin,
    card_gap,
    content_width,
    std::clamp(unit * 11, 80, 128),
    std::clamp(unit * 2, 16, 24),
    std::clamp(brand_font->line_height + runtime_status_font->line_height + 18, 120, 180),
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

  // Fixed top status bar. It is updated by a LVGL timer from the backend
  // snapshot; no worker thread ever touches LVGL objects.
  lv_obj_t* status_bar = lv_obj_create(screen);
  lv_obj_set_pos(status_bar, 0, 0);
  lv_obj_set_size(status_bar, width, status_height);
  set_surface_style(status_bar, background);
  lv_obj_set_style_pad_all(status_bar, 0, LV_PART_MAIN);
  disable_scrolling(status_bar);

  status_time_label = lv_label_create(status_bar);
  lv_label_set_text(status_time_label, "");
  lv_obj_align(status_time_label, LV_ALIGN_TOP_LEFT, outer_margin,
               status_top_padding + (status_content_height - runtime_status_font->line_height) / 2);
  lv_obj_set_style_text_color(status_time_label, primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(status_time_label, runtime_status_font, LV_PART_MAIN);

  // Keep the FontAwesome battery glyph separate from the WQY text label:
  // TinyTTF supplies the CJK/Latin text, while Montserrat supplies symbols.
  battery_value_label = lv_label_create(status_bar);
  lv_label_set_text(battery_value_label, "--%");
  lv_obj_set_style_text_color(battery_value_label, primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(battery_value_label, runtime_status_font, LV_PART_MAIN);
  lv_obj_align(battery_value_label, LV_ALIGN_TOP_RIGHT, -outer_margin,
               status_top_padding + (status_content_height - runtime_status_font->line_height) / 2);

  battery_icon = lv_label_create(status_bar);
  lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_FULL);
  lv_obj_set_style_text_color(battery_icon, primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(battery_icon, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_align_to(battery_icon, battery_value_label, LV_ALIGN_OUT_LEFT_MID, -8, 0);

  battery_charge_icon = lv_label_create(status_bar);
  lv_label_set_text(battery_charge_icon, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(battery_charge_icon, primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(battery_charge_icon, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_align(
      battery_charge_icon, LV_ALIGN_TOP_RIGHT, -outer_margin,
      status_top_padding + (status_content_height - lv_font_montserrat_24.line_height) / 2);
  lv_obj_add_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);

  // The page layer itself never scrolls.  Each page creates its own fixed
  // heading and, when needed, a separate scrollable content area inside it.
  page_layer = lv_obj_create(screen);
  lv_obj_set_pos(page_layer, 0, status_height);
  lv_obj_set_size(page_layer, width, std::max(1, height - status_height - nav_height));
  set_surface_style(page_layer, background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(page_layer, 0, LV_PART_MAIN);
  disable_scrolling(page_layer);

  show_home_page();

  // Fixed bottom navigation: a separate back button and a centered pill.
  lv_obj_t* navigation = lv_obj_create(screen);
  lv_obj_set_pos(navigation, 0, height - nav_height);
  lv_obj_set_size(navigation, width, nav_height);
  set_surface_style(navigation, background);
  lv_obj_set_style_pad_all(navigation, 0, LV_PART_MAIN);
  disable_scrolling(navigation);

  // The back button and the pill are one visual navigation group and use
  // the same control height, as in the design reference.
  const int navigation_control_size = std::clamp(nav_height * 76 / 100, 112, 172);
  const int nav_button_size = navigation_control_size;
  const int pill_height = navigation_control_size;
  const int pill_width = std::clamp(landscape ? width * 40 / 100 : width * 58 / 100, 238, 620);
  const int navigation_gap = std::clamp(unit * 2, 16, 28);
  const int navigation_group_width = nav_button_size + navigation_gap + pill_width;
  const int navigation_group_left = std::max(0, (width - navigation_group_width) / 2);
  const int navigation_bottom_padding = std::clamp(unit * 2, 12, 24);
  const int navigation_content_height = std::max(1, nav_height - navigation_bottom_padding);
  const int navigation_group_top = std::max(0, (navigation_content_height - nav_button_size) / 2);
  const int pill_top = std::max(0, (navigation_content_height - pill_height) / 2);

  lv_obj_t* back = create_nav_button(navigation, LV_SYMBOL_LEFT, nav_button_size, true, nav_color,
                                     primary_text, back_navigation);
  lv_obj_set_pos(back, navigation_group_left, navigation_group_top);

  lv_obj_t* pill = lv_obj_create(navigation);
  lv_obj_set_size(pill, pill_width, pill_height);
  // Treat the back button and pill as one navigation group.  Centering only
  // the pill leaves the complete navigation visibly shifted to the right.
  lv_obj_set_pos(pill, navigation_group_left + nav_button_size + navigation_gap, pill_top);
  set_surface_style(pill, nav_color);
  lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(pill, pill_height / 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(pill, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(pill, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(pill, 3, LV_PART_MAIN);
  lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  // Each navigation item owns exactly one third of the pill.  This keeps
  // the three icons evenly distributed regardless of the display width.
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 0, LV_PART_MAIN);
  disable_scrolling(pill);

  const int pill_item_width = pill_width / 3;
  const int pill_icon_size = std::min(pill_height - 14, 108);
  create_nav_button(pill, LV_SYMBOL_HOME, pill_icon_size, false, nav_color, primary_text,
                    home_navigation, pill_item_width, pill_height);
  create_nav_button(pill, LV_SYMBOL_FILE, pill_icon_size, false, nav_color, secondary_text,
                    log_navigation, pill_item_width, pill_height);
  create_nav_button(pill, LV_SYMBOL_POWER, pill_icon_size, false, nav_color, secondary_text,
                    power_navigation, pill_width - pill_item_width * 2, pill_height);
}

static void shutdown_gui2(void) {
  if (status_provider != nullptr) {
    status_provider->stop();
    status_provider.reset();
  }

  lv_deinit();
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
  battery_charge_icon = nullptr;
  status_time_label = nullptr;
  battery_value_label = nullptr;
  ev_exit();
  gr_exit();
}

int gui2_start(const gui2_context* context) {
  if (context == nullptr || context->settings == nullptr || context->hardware == nullptr)
    return GUI2_EXIT_INITIALIZATION_FAILED;

  settings = context->settings;
  hardware = context->hardware;
  current_language = language_from_code(settings->get_string("tw_language", "en"));
  pending_language = current_language;
  switch_to_legacy = false;

  if (gr_init() < 0) return GUI2_EXIT_INITIALIZATION_FAILED;

  ev_init();
  lv_init();
  lv_tick_set_cb(monotonic_ms);
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

  // Keep input latency independent from the display refresh period. The
  // display cadence follows TW_FRAMERATE, while pointer and wheel events
  // are sampled every few milliseconds.
  lv_timer_set_period(lv_indev_get_read_timer(pointer_indev), 5);

#if LV_USE_GESTURE_RECOGNITION
  // Make pinch recognition responsive enough for a phone-sized display.
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
    const uint32_t loop_start_ms = monotonic_ms();
    perf_manager.Update();
    uint32_t delay_ms = lv_timer_handler();
    if (gui2_input_take_activity()) perf_manager.NotifyInteraction();
    const int wheel = gui2_input_take_wheel();
    if (wheel != 0 && main_content != nullptr) {
      lv_point_t point;
      lv_indev_get_point(pointer_indev, &point);
      lv_area_t content_area;
      lv_obj_get_coords(main_content, &content_area);

      const bool over_content = point.x >= content_area.x1 && point.x <= content_area.x2 &&
                                point.y >= content_area.y1 && point.y <= content_area.y2;
      if (over_content) {
        // Linux REL_WHEEL is positive for wheel-up. LVGL's scroll
        // offset increases when content moves down.
        lv_obj_scroll_by_bounded(main_content, 0, wheel * 80, LV_ANIM_OFF);
      }

      // The wheel event was collected by the input timer inside the
      // first handler call.  Render the resulting scroll immediately,
      // before sleeping until the next timer deadline.
      const uint32_t wheel_delay_ms = lv_timer_handler();
      if (wheel_delay_ms < delay_ms) delay_ms = wheel_delay_ms;
    }

    if (gui2_display_present()) perf_manager.NotifyFrameActivity();
    delay_ms = perf_manager.ClampTimeoutMs(delay_ms);

    if (delay_ms == LV_NO_TIMER_READY || delay_ms > 50) delay_ms = 10;
    if (delay_ms == 0) delay_ms = 1;

    // gr_flip() waits for the DRM page flip on the normal path.  Account
    // for that time, otherwise a nominal 16 ms LVGL delay would be added
    // after an already 16 ms vsync wait and cap the UI near 30 FPS.
    const uint32_t elapsed_ms = monotonic_ms() - loop_start_ms;
    if (delay_ms > elapsed_ms) usleep((delay_ms - elapsed_ms) * 1000);
  }

  shutdown_gui2();
  return switch_to_legacy ? GUI2_EXIT_TO_LEGACY : 0;
}
