#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#include "app/gui2_lifecycle.h"
#include "app/gui2_loop.h"
#include "app/gui2_runtime.h"
#include "app/screen_actions.h"
#include "backend/hardware_settings.h"
#include "backend/screen_backend.h"
#include "components/apply_button.h"
#include "components/choice_card.h"
#include "components/icon.h"
#include "components/quick_action_button.h"
#include "components/section_label.h"
#include "components/slider.h"
#include "components/slider_card.h"
#include "core/page_transition.h"
#include "core/ui_event_guard.h"
#include "core/ui_helpers.h"
#include "core/ui_metrics.h"
#include "core/wheel_scroll.h"
#include "gui2.h"
#include "gui2_display.h"
#include "gui2_input.h"
#include "gui2_svg_assets.h"
#include "gui2_svg_cache.h"
#include "gui/twmsg.h"
#include "i18n/console_strings.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/action_definitions.h"
#include "pages/action_page.h"
#include "pages/advanced_page.h"
#include "pages/console_page.h"
#include "pages/backup_page.h"
#include "pages/decrypt_page.h"
#include "pages/mount_page.h"
#include "pages/export_log_page.h"
#include "pages/hardware_page.h"
#include "pages/home_page.h"
#include "pages/language_page.h"
#include "pages/page_router.h"
#include "pages/page_state.h"
#include "pages/reboot_page.h"
#include "pages/settings_data.h"
#include "pages/settings_page.h"
#include "pages/wipe_page.h"
#include "pages/progress_page.h"
#include "pages/timezone_logic.h"
#include "pages/timezone_page.h"
#include "shell/bottom_navigation.h"
#include "shell/confirm_dialog.h"
#include "shell/gui_shell_base.h"
#include "shell/mouse_cursor.h"
#include "shell/navigation_fade.h"
#include "shell/page_host.h"
#include "shell/page_scaffold.h"
#include "shell/quick_panel.h"
#include "shell/quick_panel_controller.h"
#include "shell/screen_feedback.h"
#include "shell/screen_lock.h"
#include "shell/status_bar.h"
#include "shell/status_bar_controller.h"
#include "twrpminui/minui.h"

// Load WQY from recovery resources.

static lv_font_t* runtime_text_font;
static lv_font_t* runtime_status_font;
static lv_font_t* runtime_console_fonts[3];
static lv_font_t* runtime_brand_font;
static gui2_app::graphics_state graphics;
static gui2_app::runtime_state runtime;
static gui2_backend::settings_store*& settings = runtime.settings;
static gui2_backend::hardware_settings*& hardware = runtime.hardware;
static gui2_backend::screen_backend*& screen = runtime.screen;
static gui2_backend::reboot_backend*& reboot = runtime.reboot;
static gui2_backend::console_backend*& console = runtime.console;
static gui2_backend::log_export_backend*& log_export = runtime.log_export;
static gui2_backend::wipe_backend*& wipe = runtime.wipe;
static gui2_backend::decrypt_backend*& decrypt = runtime.decrypt;
static gui2_backend::backup_backend*& backup = runtime.backup;
static gui2_backend::mount_backend*& mount = runtime.mount;
static gui2_shell::status_bar_controller status_controller;

using gui2_core::card_inner_padding;
using gui2_core::navigation_safe_area;
using gui2_core::single_line_card_height;
using gui2_core::ui;
using gui2_core::ui_px;

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
static lv_obj_t* page_layer;
static lv_obj_t* page_summary;
static gui2_shell::page_host page_host;
static gui2_pages::page_state page_state;
static gui2_shell::status_bar_view status_view;
static gui2_shell::bottom_navigation_view navigation_view;
static lv_obj_t* mouse_cursor;
static lv_obj_t* legacy_dialog;
static gui2_shell::quick_panel_view quick_panel_view;
static gui2_shell::quick_panel_controller quick_panel_controller;
static lv_obj_t* quick_record_button;
static lv_obj_t* quick_record_label;
static lv_obj_t* quick_feedback;
static bool quick_feedback_pending;
static gui2_shell::screen_feedback screen_feedback;
static gui2_shell::screen_lock screen_lock;
static gui2_app::screen_actions screen_actions;
static gui2_core::wheel_scroll_controller wheel_scroll_controller;
static bool home_page_active;
static bool home_navigation_active;
static bool& switch_to_legacy = runtime.switch_to_legacy;
static bool& reboot_requested = runtime.reboot_requested;

using app_language = gui2_i18n::language_id;
using language_pack = gui2_i18n::language_pack;
using gui2_pages::action_definition;
using gui2_pages::action_id;
using page_kind = gui2_pages::page_id;
using gui2_core::page_transition;
using gui2_pages::format_indices;
using gui2_pages::offset_indices;
using gui2_pages::recording_fps_at;
using gui2_pages::recording_fps_count;
using gui2_pages::recording_fps_limit;
using gui2_pages::recording_fps_values;
using gui2_pages::timezone_indices;
using gui2_pages::timezone_offsets;
using gui2_pages::timezone_values;

static app_language& current_language = page_state.current_language;
static app_language& pending_language = page_state.pending_language;
static void route_page(const gui2_pages::page_request& request);
static gui2_pages::page_router page_router(route_page);

static const language_pack& strings(void) {
  return gui2_i18n::get_language_pack(current_language);
}

// Installed into the legacy message catalogue so console output follows the UI
// language. Loading the legacy language XML instead walks into its font
// overrides, which no longer have a font stack to override.
static std::string console_translator(const std::string& name) {
  const char* text = gui2_i18n::console_string_for(current_language, name);
  return text == nullptr ? std::string() : std::string(text);
}

using gui2_components::create_svg_image;

static lv_obj_t* (&language_option_cards)[3] = page_state.language_option_cards;
static lv_obj_t* (&language_check_labels)[3] = page_state.language_check_labels;
static constexpr app_language language_values[3] = {
  app_language::ENGLISH,
  app_language::ZH_CN,
  app_language::ZH_TW,
};

static void show_home_page(page_transition transition);
static void show_action_page(const action_definition& definition, page_transition transition);
static void show_reboot_page(page_transition transition);
static void show_language_page(page_transition transition);
static void show_timezone_page(page_transition transition);
static void show_brightness_page(page_transition transition);
static void show_haptics_page(page_transition transition);
static void show_recording_page(page_transition transition);
static void show_console_page(page_transition transition);
static void show_export_log_page(page_transition transition);
static void show_console_settings_page(page_transition transition);
static void show_wipe_page(page_transition transition);
static void show_advanced_wipe_page(page_transition transition);
static void show_format_data_page(page_transition transition);
static void show_wipe_progress_page(page_transition transition);
static void refresh_wipe_progress(void);
static void show_decrypt_page(page_transition transition);
static void show_decrypt_progress_page(page_transition transition);
static void refresh_decrypt_progress(void);
static void show_backup_page(page_transition transition);
static void show_backup_progress_page(page_transition transition);
static void show_mount_page(page_transition transition);
static void refresh_backup_progress(void);
static void create_gui2_shell(lv_obj_t* screen);
static void close_quick_menu(void);
static void refresh_recording_ui(void);
static void status_gesture_event_cb(lv_event_t* event);
static void quick_panel_gesture_event_cb(lv_event_t* event);
static void create_quick_menu(void);
static void screen_lock_unlocked(void* user_data);
static void reboot_option_event_cb(lv_event_t* event);
static void reboot_slot_event_cb(lv_event_t* event);
static void reboot_confirmation_complete(void* user_data);

static void navigate_to(page_kind page, const void* payload = nullptr,
                        page_transition transition = page_transition::PUSH) {
  if (page_router.current() == page_kind::REBOOT) page_state.reboot.confirmation_slider.detach();
  page_router.navigate(page, payload, transition);
}

static int& pending_timezone_index = page_state.pending_timezone_index;
static int& pending_offset_index = page_state.pending_offset_index;
static bool& pending_military_time = page_state.pending_military_time;
static bool& pending_dst = page_state.pending_dst;
static lv_obj_t* (&timezone_cards)[24] = page_state.timezone_cards;
static lv_obj_t* (&offset_cards)[4] = page_state.offset_cards;
static lv_obj_t* (&format_cards)[2] = page_state.format_cards;
static lv_obj_t*& dst_card = page_state.dst_card;
static lv_obj_t*& current_timezone_label = page_state.current_timezone_label;
static lv_obj_t*& hardware_error_label = page_state.hardware_error_label;
static bool& hardware_settings_dirty = page_state.hardware_settings_dirty;
using hardware_slider_binding = gui2_pages::hardware_slider_binding;
static hardware_slider_binding& brightness_binding = page_state.brightness_binding;
static hardware_slider_binding& screen_timeout_binding = page_state.screen_timeout_binding;
static hardware_slider_binding& console_font_binding = page_state.console_font_binding;
static hardware_slider_binding& quick_brightness_binding = page_state.quick_brightness_binding;
static hardware_slider_binding (&haptic_bindings)[3] = page_state.haptic_bindings;
static hardware_slider_binding& recording_binding = page_state.recording_binding;
static bool& quick_brightness_dirty = page_state.quick_brightness_dirty;

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

using gui2_core::accept_click;
using gui2_core::add_press_cancel_guard;
using gui2_core::clear_click_guard;
using gui2_core::disable_scrolling;
using gui2_core::press_cancel_guard_cb;
using gui2_core::set_surface_style;

static void action_card_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  const auto* definition = static_cast<const action_definition*>(lv_event_get_user_data(event));
  if (definition != nullptr) navigate_to(page_kind::ACTION, definition, page_transition::PUSH);
}

static void reset_reboot_page_state(void) {
  page_state.reboot.target_selected = false;
  page_state.reboot.has_error = false;
  page_state.reboot.selected_target = gui2_backend::reboot_target::SYSTEM;
}

static void reboot_option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* target =
      static_cast<const gui2_backend::reboot_target*>(lv_event_get_user_data(event));
  if (target == nullptr) return;

  page_state.reboot.selected_target = *target;
  page_state.reboot.target_selected = true;
  page_state.reboot.has_error = false;
  navigate_to(page_kind::REBOOT, nullptr, page_transition::REPLACE);
}

static void reboot_slot_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* slot = static_cast<const gui2_backend::boot_slot*>(lv_event_get_user_data(event));
  if (slot == nullptr || reboot == nullptr) return;

  page_state.reboot.has_error = !reboot->set_active_slot(*slot);
  navigate_to(page_kind::REBOOT, nullptr, page_transition::REPLACE);
}

static void reboot_confirmation_complete(void* user_data) {
  auto* state = static_cast<gui2_pages::reboot_page_state*>(user_data);
  if (state == nullptr || reboot == nullptr || !state->target_selected) return;

  if (!reboot->request_reboot(state->selected_target)) {
    state->has_error = true;
    navigate_to(page_kind::REBOOT, nullptr, page_transition::REPLACE);
    return;
  }
  reboot_requested = true;
}

enum class settings_target {
  LANGUAGE,
  TIMEZONE,
  BRIGHTNESS,
  HAPTICS,
  RECORDING,
  LEGACY,
  EXPORT_LOG,
  CONSOLE_SETTINGS,
  ADVANCED_WIPE,
  FORMAT_DATA,
};

static constexpr settings_target language_target = settings_target::LANGUAGE;
static constexpr settings_target timezone_target = settings_target::TIMEZONE;
static constexpr settings_target brightness_target = settings_target::BRIGHTNESS;
static constexpr settings_target haptics_target = settings_target::HAPTICS;
static constexpr settings_target recording_target = settings_target::RECORDING;
static constexpr settings_target legacy_target = settings_target::LEGACY;
static constexpr settings_target export_log_target = settings_target::EXPORT_LOG;
static constexpr settings_target console_settings_target = settings_target::CONSOLE_SETTINGS;
static constexpr settings_target advanced_wipe_target = settings_target::ADVANCED_WIPE;
static constexpr settings_target format_data_target = settings_target::FORMAT_DATA;
static constexpr int wipe_target_indices[24] = {
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
};

static int navigation_rank(page_kind page) {
  if (page == page_kind::HOME) return 0;
  if (page == page_kind::CONSOLE) return 1;
  if (page == page_kind::REBOOT) return 2;
  return -1;
}

static page_transition navigation_transition(page_kind target) {
  const int current = navigation_rank(page_router.current());
  const int from = current < 0 ? navigation_rank(page_kind::CONSOLE) : current;
  const int to = navigation_rank(target);
  if (to < 0 || from == to) return page_transition::PUSH;
  return to > from ? page_transition::PUSH : page_transition::POP;
}

static void navigate_back(void) {
  close_quick_menu();
  if (page_router.current() == page_kind::REBOOT) {
    const auto return_request = page_state.reboot.return_request;
    navigate_to(return_request.id, return_request.payload, page_transition::POP);
  } else if (page_router.current() == page_kind::LANGUAGE ||
             page_router.current() == page_kind::TIMEZONE ||
             page_router.current() == page_kind::BRIGHTNESS ||
             page_router.current() == page_kind::HAPTICS ||
             page_router.current() == page_kind::RECORDING ||
             page_router.current() == page_kind::CONSOLE_SETTINGS) {
    if (page_router.current() == page_kind::LANGUAGE && page_state.language_from_decrypt) {
      page_state.language_from_decrypt = false;
      navigate_to(page_kind::DECRYPT, nullptr, page_transition::POP);
      return;
    }
    navigate_to(page_kind::ACTION,
                &gui2_pages::action_definitions()[static_cast<int>(action_id::SETTINGS)],
                page_transition::POP);
  } else if (page_router.current() == page_kind::ADVANCED_WIPE ||
             page_router.current() == page_kind::FORMAT_DATA) {
    navigate_to(page_kind::WIPE, nullptr, page_transition::POP);
  } else if (page_router.current() == page_kind::BACKUP_PROGRESS) {
    if (backup == nullptr || backup->status().state != gui2_backend::backup_state::RUNNING) {
      if (backup != nullptr) backup->acknowledge();
      navigate_to(page_kind::HOME, nullptr, page_transition::POP);
    }
  } else if (page_router.current() == page_kind::WIPE_PROGRESS) {
    if (wipe == nullptr || wipe->status().state != gui2_backend::wipe_state::RUNNING)
      navigate_to(page_kind::HOME, nullptr, page_transition::POP);
  } else if (page_router.current() == page_kind::EXPORT_LOG) {
    navigate_to(page_kind::ACTION,
                &gui2_pages::action_definitions()[static_cast<int>(action_id::ADVANCED)],
                page_transition::POP);
  } else if (page_router.current() == page_kind::DECRYPT) {
    if (decrypt != nullptr && decrypt->is_encrypted() && decrypt->start_refresh()) {
      page_state.decrypt_refreshing = true;
      page_state.progress_settled_ms = 0;
      navigate_to(page_kind::DECRYPT_PROGRESS, nullptr, page_transition::PUSH);
    } else {
      navigate_to(page_kind::HOME, nullptr, page_transition::POP);
    }
  } else if (!home_page_active) {
    navigate_to(page_kind::HOME, nullptr, page_transition::POP);
  }
}

static void navigation_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (!accept_click(event)) return;

  const auto* action =
      static_cast<const gui2_shell::navigation_action*>(lv_event_get_user_data(event));
  if (action == nullptr) return;

  close_quick_menu();

  if (*action == gui2_shell::navigation_action::POWER) {
    if (page_router.current() != page_kind::REBOOT) {
      const page_transition transition = navigation_transition(page_kind::REBOOT);
      page_state.reboot.return_request = page_router.current_request();
      reset_reboot_page_state();
      navigate_to(page_kind::REBOOT, nullptr, transition);
    }
    return;
  }

  if (*action == gui2_shell::navigation_action::LOG) {
    if (page_router.current() != page_kind::CONSOLE)
      navigate_to(page_kind::CONSOLE, nullptr, navigation_transition(page_kind::CONSOLE));
    return;
  }

  if (*action == gui2_shell::navigation_action::BACK ||
      (*action == gui2_shell::navigation_action::HOME && !home_page_active)) {
    if (*action == gui2_shell::navigation_action::HOME) {
      navigate_to(page_kind::HOME, nullptr, navigation_transition(page_kind::HOME));
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
  status_controller.refresh();
  if (page_state.language_from_decrypt) {
    page_state.language_from_decrypt = false;
    navigate_to(page_kind::DECRYPT, nullptr, page_transition::POP);
    return;
  }
  navigate_to(page_kind::ACTION,
              &gui2_pages::action_definitions()[static_cast<int>(action_id::SETTINGS)],
              page_transition::POP);
}

static void refresh_time_choices(void) {
  gui2_pages::refresh_time_choices(format_cards, pending_military_time, timezone_cards,
                                   pending_timezone_index, offset_cards, pending_offset_index,
                                   dst_card, pending_dst, ui.card_color);
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

static void apply_timezone_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event) || settings == nullptr)
    return;

  const bool saved =
      settings->set_persistent("tw_military_time", pending_military_time ? "1" : "0") &&
      settings->set_persistent("tw_time_zone_guisel", timezone_values[pending_timezone_index]) &&
      settings->set_persistent("tw_time_zone_guioffset", timezone_offsets[pending_offset_index]) &&
      settings->set_persistent("tw_time_zone_guidst", pending_dst ? "1" : "0") &&
      settings->set_persistent("tw_time_zone",
                               gui2_pages::build_timezone_value(timezone_values, timezone_offsets,
                                                                pending_timezone_index,
                                                                pending_offset_index, pending_dst));
  if (saved) {
    settings->update_timezone();
    if (!settings->flush()) return;
    status_controller.refresh();
  }
  navigate_to(page_kind::TIMEZONE, nullptr, page_transition::REPLACE);
}

static void legacy_dialog_cancel_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  auto* dialog = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (dialog != nullptr) {
    lv_obj_delete(dialog);
    if (dialog == legacy_dialog) legacy_dialog = nullptr;
  }
}

static void legacy_dialog_confirm_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  switch_to_legacy = true;
  auto* dialog = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (dialog != nullptr) {
    lv_obj_delete(dialog);
    if (dialog == legacy_dialog) legacy_dialog = nullptr;
  }
}

static void request_legacy_gui_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  gui2_shell::confirm_dialog_options options;
  options.metrics = &ui;
  options.title = strings().classic_gui_confirm_title;
  options.body = strings().classic_gui_confirm_body;
  options.cancel = strings().cancel;
  options.confirm = strings().classic_gui_confirm;
  options.cancel_callback = legacy_dialog_cancel_event_cb;
  options.confirm_callback = legacy_dialog_confirm_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  legacy_dialog = gui2_shell::create_confirm_dialog(options);
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

  if (quick_panel_controller.is_open()) {
    quick_feedback_pending = false;
    gui2_shell::set_quick_panel_feedback_visible(&quick_panel_view, true);
    quick_panel_controller.sync_geometry(quick_panel_view);
  } else {
    quick_feedback_pending = true;
  }
}

static void screenshot_result_cb(void*, const gui2_backend::capture_result& result) {
  if (result.success)
    set_quick_feedback(strings().screenshot_saved, result.path.c_str());
  else
    set_quick_feedback(strings().screenshot_failed);
}

static void quick_action_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (quick_panel_controller.gesture_moved()) {
    clear_click_guard();
    return;
  }
  if (!accept_click(event) || screen == nullptr) return;
  const auto* action = static_cast<const quick_action*>(lv_event_get_user_data(event));
  if (action == nullptr) return;

  if (*action == quick_action::SCREENSHOT) {
    // Close before capturing.
    close_quick_menu();
    screen_actions.request_screenshot();
    return;
  }

  if (*action == quick_action::SCREEN_OFF) {
    // Defer blanking until the touch frame is presented.
    close_quick_menu();
    screen_actions.request_screen_off();
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
  if (status_view.recording_indicator != nullptr) {
    lv_label_set_text(status_view.recording_indicator, strings().recording_indicator);
    if (recording)
      lv_obj_clear_flag(status_view.recording_indicator, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(status_view.recording_indicator, LV_OBJ_FLAG_HIDDEN);
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
  gui2_shell::layout_status_bar(status_view, ui);
}

static void refresh_quick_panel_on_open(void) {
  gui2_shell::set_quick_panel_feedback_visible(&quick_panel_view, quick_feedback_pending);
  quick_feedback_pending = false;
  quick_panel_controller.sync_geometry(quick_panel_view);
  refresh_recording_ui();
  sync_quick_brightness();
}

static bool quick_menu_input_active(void) {
  return quick_panel_controller.input_active();
}

static void queue_wheel_scroll(int amount) {
  wheel_scroll_controller.queue(amount, main_content, quick_menu_input_active());
}

static void advance_wheel_scroll(uint64_t now_ms) {
  wheel_scroll_controller.advance(now_ms, main_content, quick_menu_input_active());
}

static void close_quick_menu(void) {
  quick_panel_controller.close();
}

static void open_quick_menu(void) {
  quick_panel_controller.open();
}

static void begin_quick_drag(lv_event_t* event, bool from_dismiss) {
  quick_panel_controller.begin_drag(event, from_dismiss);
}

static void update_quick_drag(lv_event_t* event) {
  quick_panel_controller.update_drag(event);
}

static void finish_quick_drag(void) {
  quick_panel_controller.finish_drag();
}

static void status_gesture_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    begin_quick_drag(event, false);
  } else if (code == LV_EVENT_PRESSING) {
    update_quick_drag(event);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (quick_panel_controller.gesture_tracking()) finish_quick_drag();
  }
}

static void quick_dismiss_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    begin_quick_drag(event, true);
  } else if (code == LV_EVENT_PRESSING) {
    update_quick_drag(event);
  } else if (code == LV_EVENT_CLICKED) {
    const bool moved = quick_panel_controller.gesture_moved();
    // Consume the click guard state, but never close or vibrate for an outside tap.
    clear_click_guard();
    if (!moved) quick_panel_controller.stop_tracking();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (quick_panel_controller.gesture_tracking()) finish_quick_drag();
  }
}

static void quick_panel_gesture_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    begin_quick_drag(event, false);
  } else if (code == LV_EVENT_PRESSING) {
    update_quick_drag(event);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (quick_panel_controller.gesture_tracking()) finish_quick_drag();
  }
}

static void create_quick_menu(void) {
  quick_brightness_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, true, false };
  quick_brightness_dirty = false;

  gui2_shell::quick_action_item entries[3];
  int action_count = 0;
  entries[action_count++] = { &kGui2IconScrShot, strings().screenshot, &screenshot_action };
  if (screen != nullptr && screen->has_screen_off())
    entries[action_count++] = { &kGui2IconLock, strings().screen_off, &screen_off_action };
  entries[action_count++] = { &kGui2IconScrRecord, strings().start_recording, &recording_action };

  gui2_shell::quick_panel_options options;
  options.metrics = &ui;
  options.title = strings().quick_menu_title;
  options.brightness_available = hardware != nullptr && hardware->has_brightness();
  options.brightness_label = strings().brightness_label;
  options.brightness_value = options.brightness_available ? hardware->brightness_percent() : 0;
  options.brightness_visual = &quick_brightness_binding.visual;
  options.brightness_value_label = &quick_brightness_binding.value_label;
  options.brightness_user_data = &quick_brightness_binding;
  options.actions = entries;
  options.action_count = action_count;
  options.recording_action_user_data = &recording_action;
  options.dismiss_event_callback = quick_dismiss_event_cb;
  options.panel_gesture_callback = quick_panel_gesture_event_cb;
  options.brightness_event_callback = quick_brightness_event_cb;
  options.brightness_state_callback = hardware_slider_state_event_cb;
  options.action_event_callback = quick_action_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  quick_panel_view = gui2_shell::create_quick_panel(options);
  quick_record_button = quick_panel_view.recording_button;
  quick_record_label = quick_panel_view.recording_label;
  quick_feedback = quick_panel_view.feedback;
  screen_feedback.attach(quick_panel_view.screenshot_flash);
  quick_panel_controller.initialize(ui, quick_panel_view, pointer_indev,
                                    refresh_quick_panel_on_open);
  if (options.brightness_available) sync_quick_brightness();
  close_quick_menu();
}

static void create_page_scaffold(page_kind page, bool is_home, const char* title,
                                 const char* summary, int bottom_reserved = 0,
                                 page_transition transition = page_transition::NONE) {
  close_quick_menu();
  for (int i = 0; i < 3; ++i) {
    language_option_cards[i] = nullptr;
    language_check_labels[i] = nullptr;
  }
  page_state.console = {};
  page_state.console_consumed = 0;
  page_state.wipe_progress = {};
  page_state.wipe_console_consumed = 0;
  page_state.decrypt_progress = {};
  page_state.decrypt_console_consumed = 0;
  page_state.format_data_input = nullptr;
  page_state.format_data_track = nullptr;
  page_state.format_data_confirm.detach();
  page_state.decrypt_pattern.detach();
  page_state.backup_tabs.detach();
  page_state.backup_confirm.detach();
  page_state.backup_view = {};
  page_state.decrypt_input = nullptr;
  page_state.decrypt_keyboard = nullptr;
  page_state.decrypt_status = nullptr;
  if (page_state.format_data_keyboard != nullptr) {
    lv_obj_delete(page_state.format_data_keyboard);
    page_state.format_data_keyboard = nullptr;
  }
  page_state.wipe_confirm.detach();
  page_state.kernel_log_card = nullptr;
  page_state.logcat_card = nullptr;
  page_state.export_result_label = nullptr;
  home_page_active = is_home;
  home_navigation_active = true;
  const auto scaffold = page_host.build(title, summary, bottom_reserved, transition);
  main_content = scaffold.content;
  page_summary = scaffold.summary;
  page_layer = page_host.current_page();
}

static void show_reboot_page(page_transition transition) {
  create_page_scaffold(page_kind::REBOOT, false, strings().reboot_title, strings().reboot_summary,
                       0, transition);

  gui2_backend::reboot_capabilities capabilities;
  if (reboot != nullptr) capabilities = reboot->capabilities();

  size_t option_count = 0;
  auto add_option = [&](gui2_backend::reboot_target target, const char* label, bool available) {
    if (!available || option_count >= std::size(page_state.reboot.options)) return;
    page_state.reboot.options[option_count++] = { target, label };
  };
  add_option(gui2_backend::reboot_target::SYSTEM, strings().reboot_system, capabilities.system);
  add_option(gui2_backend::reboot_target::POWER_OFF, strings().reboot_power_off,
             capabilities.power_off);
  add_option(gui2_backend::reboot_target::RECOVERY, strings().reboot_recovery,
             capabilities.recovery);
  add_option(gui2_backend::reboot_target::FASTBOOT, strings().reboot_fastboot,
             capabilities.fastboot);
  add_option(gui2_backend::reboot_target::BOOTLOADER, strings().reboot_bootloader,
             capabilities.bootloader);
  add_option(gui2_backend::reboot_target::DOWNLOAD, strings().reboot_download,
             capabilities.download);
  add_option(gui2_backend::reboot_target::EDL, strings().reboot_edl, capabilities.edl);

  const std::string active_slot = reboot == nullptr ? std::string() : reboot->active_slot();
  const std::string current_slot_text = std::string(strings().current_boot_slot) + ": " +
                                        (active_slot.empty() ? std::string("-") : active_slot);
  gui2_pages::reboot_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.options = page_state.reboot.options;
  options.option_count = option_count;
  options.target_selected = page_state.reboot.target_selected;
  options.selected_target = page_state.reboot.selected_target;
  options.error_text = page_state.reboot.has_error ? strings().reboot_failed : nullptr;
  options.has_boot_slots = capabilities.boot_slots;
  options.current_slot_text = current_slot_text.c_str();
  options.active_slot = &active_slot;
  options.slots = page_state.reboot.slots;
  options.slot_count = std::size(page_state.reboot.slots);
  options.option_event_callback = reboot_option_event_cb;
  options.slot_event_callback = reboot_slot_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  options.confirmation_slider = &page_state.reboot.confirmation_slider;
  options.confirmation_callback = reboot_confirmation_complete;
  options.confirmation_user_data = &page_state.reboot;
  gui2_pages::build_reboot_page(options);
}

static void settings_option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  const auto* target = static_cast<const settings_target*>(lv_event_get_user_data(event));
  if (target == nullptr) return;
  if (*target == settings_target::LANGUAGE)
    navigate_to(page_kind::LANGUAGE);
  else if (*target == settings_target::TIMEZONE)
    navigate_to(page_kind::TIMEZONE);
  else if (*target == settings_target::BRIGHTNESS)
    navigate_to(page_kind::BRIGHTNESS);
  else if (*target == settings_target::HAPTICS)
    navigate_to(page_kind::HAPTICS);
  else if (*target == settings_target::RECORDING)
    navigate_to(page_kind::RECORDING);
  else if (*target == settings_target::EXPORT_LOG)
    navigate_to(page_kind::EXPORT_LOG);
  else if (*target == settings_target::CONSOLE_SETTINGS)
    navigate_to(page_kind::CONSOLE_SETTINGS);
  else if (*target == settings_target::ADVANCED_WIPE)
    navigate_to(page_kind::ADVANCED_WIPE);
  else if (*target == settings_target::FORMAT_DATA)
    navigate_to(page_kind::FORMAT_DATA);
  else
    request_legacy_gui_event_cb(event);
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
    lv_label_set_text_fmt(binding.value_label, "%d FPS", recording_fps_at(screen, value));
  } else if (binding.brightness) {
    lv_label_set_text_fmt(binding.value_label, "%d%%", value);
  } else if (binding.screen_timeout) {
    const int seconds = gui2_pages::screen_timeout_at(value);
    if (seconds <= 0)
      lv_label_set_text(binding.value_label, strings().screen_timeout_never);
    else if (seconds % 60 == 0)
      lv_label_set_text_fmt(binding.value_label, "%d min", seconds / 60);
    else
      lv_label_set_text_fmt(binding.value_label, "%d s", seconds);
  } else if (binding.console_font) {
    lv_label_set_text(binding.value_label, strings().console_font_steps[std::clamp(value, 0, 2)]);
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
  if (binding == nullptr || slider == nullptr ||
      (hardware == nullptr && !binding->recording_fps && !binding->screen_timeout &&
       !binding->console_font))
    return;

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    const int value = gui2_components::get_value(&binding->visual);
    bool applied = false;
    if (binding->recording_fps) {
      applied = screen != nullptr && screen->set_recording_fps(recording_fps_at(screen, value));
    } else if (binding->screen_timeout) {
      page_state.screen_timeout_index = std::clamp(value, 0, gui2_pages::screen_timeout_count - 1);
      applied = settings != nullptr &&
                settings->set_persistent(
                    "tw_screen_timeout_secs",
                    std::to_string(gui2_pages::screen_timeout_at(page_state.screen_timeout_index)));
    } else if (binding->console_font) {
      page_state.console_font_index = std::clamp(value, 0, 2);
      applied = settings != nullptr &&
                settings->set_persistent("tw_gui2_console_font",
                                         std::to_string(page_state.console_font_index));
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

static gui2_pages::hardware_page_view create_hardware_page(
    const gui2_pages::hardware_slider_spec* sliders, size_t slider_count) {
  gui2_pages::hardware_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.sliders = sliders;
  options.slider_count = slider_count;
  options.value_changed_callback = hardware_slider_event_cb;
  options.pressed_callback = hardware_slider_state_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  const auto view = gui2_pages::build_hardware_page(options);
  hardware_error_label = view.error_label;
  return view;
}

static void show_brightness_page(page_transition transition) {
  hardware_settings_dirty = false;
  brightness_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, true, false, false, false };
  screen_timeout_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false, false, true,
                             false };
  create_page_scaffold(page_kind::BRIGHTNESS, false, strings().screen_title,
                       strings().screen_summary, 0, transition);

  const bool has_brightness = hardware != nullptr && hardware->has_brightness();
  const bool has_timeout = screen != nullptr && screen->has_screen_off();
  gui2_pages::hardware_slider_spec sliders[3];
  size_t slider_count = 0;
  hardware_slider_binding* bindings[3] = {};
  if (has_brightness) {
    bindings[slider_count] = &brightness_binding;
    sliders[slider_count++] = { strings().brightness_label,
                                10,
                                100,
                                hardware->brightness_percent(),
                                &brightness_binding.visual,
                                &brightness_binding.value_label,
                                &brightness_binding };
  }
  if (has_timeout) {
    const int stored = settings == nullptr ? 60 : settings->get_int("tw_screen_timeout_secs", 60);
    page_state.screen_timeout_index = gui2_pages::screen_timeout_index_for(stored);
    bindings[slider_count] = &screen_timeout_binding;
    sliders[slider_count++] = { strings().screen_timeout_label,
                                0,
                                gui2_pages::screen_timeout_count - 1,
                                page_state.screen_timeout_index,
                                &screen_timeout_binding.visual,
                                &screen_timeout_binding.value_label,
                                &screen_timeout_binding };
  }

  const auto page = create_hardware_page(sliders, slider_count);
  for (size_t i = 0; i < slider_count; ++i) {
    update_hardware_slider_value(*bindings[i], gui2_components::get_value(&bindings[i]->visual));
    gui2_components::refresh_slider(&bindings[i]->visual);
  }
  lv_obj_update_layout(page.body);
}

static void show_console_settings_page(page_transition transition) {
  hardware_settings_dirty = false;
  console_font_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false, false, false,
                           true };
  create_page_scaffold(page_kind::CONSOLE_SETTINGS, false, strings().console_settings_title,
                       strings().console_settings_summary, 0, transition);

  page_state.console_font_index =
      settings == nullptr ? 1 : std::clamp(settings->get_int("tw_gui2_console_font", 1), 0, 2);
  gui2_pages::hardware_slider_spec slider = { strings().console_font_label,
                                              0,
                                              2,
                                              page_state.console_font_index,
                                              &console_font_binding.visual,
                                              &console_font_binding.value_label,
                                              &console_font_binding };
  const auto page = create_hardware_page(&slider, 1);
  update_hardware_slider_value(console_font_binding,
                               gui2_components::get_value(&console_font_binding.visual));
  gui2_components::refresh_slider(&console_font_binding.visual);
  lv_obj_update_layout(page.body);
}

static void show_haptics_page(page_transition transition) {
  hardware_settings_dirty = false;
  for (auto& binding : haptic_bindings)
    binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false, false };
  create_page_scaffold(page_kind::HAPTICS, false, strings().haptics_title,
                       strings().haptics_summary, 0, transition);
  const gui2_backend::haptic_channel channels[3] = {
    gui2_backend::haptic_channel::BUTTON,
    gui2_backend::haptic_channel::KEYBOARD,
    gui2_backend::haptic_channel::ACTION,
  };
  const char* labels[3] = { strings().button_haptics, strings().keyboard_haptics,
                            strings().action_haptics };
  gui2_pages::hardware_slider_spec sliders[3];
  for (int i = 0; i < 3; ++i) {
    haptic_bindings[i].channel = channels[i];
    const int maximum = i == 2 ? 500 : 300;
    const int value = hardware == nullptr ? 0 : hardware->haptic_duration_ms(channels[i]);
    sliders[i] = { labels[i],
                   0,
                   maximum,
                   value,
                   &haptic_bindings[i].visual,
                   &haptic_bindings[i].value_label,
                   &haptic_bindings[i] };
  }
  const auto page = create_hardware_page(sliders, 3);
  for (int i = 0; i < 3; ++i) {
    update_hardware_slider_value(haptic_bindings[i],
                                 gui2_components::get_value(&haptic_bindings[i].visual));
    gui2_components::refresh_slider(&haptic_bindings[i].visual);
  }
  lv_obj_update_layout(page.body);
}

static void show_recording_page(page_transition transition) {
  hardware_settings_dirty = false;
  recording_binding = { nullptr, gui2_backend::haptic_channel::BUTTON, false, true };
  create_page_scaffold(page_kind::RECORDING, false, strings().recording_settings_title,
                       strings().recording_settings_summary, 0, transition);
  int fps = screen == nullptr ? 30 : screen->recording_fps();
  int index = 2;
  for (int i = 0; i < recording_fps_count(screen); ++i) {
    if (recording_fps_at(screen, i) == fps) {
      index = i;
      break;
    }
  }
  gui2_pages::hardware_slider_spec slider = { strings().recording_fps_label,
                                              0,
                                              recording_fps_count(screen) - 1,
                                              index,
                                              &recording_binding.visual,
                                              &recording_binding.value_label,
                                              &recording_binding };
  const auto page = create_hardware_page(&slider, 1);
  update_hardware_slider_value(recording_binding,
                               gui2_components::get_value(&recording_binding.visual));
  gui2_components::refresh_slider(&recording_binding.visual);
  lv_obj_update_layout(page.body);
}

static void poll_console(bool scroll_to_end) {
  if (console == nullptr || page_state.console.body == nullptr) return;
  std::vector<gui2_backend::console_line> lines;
  const size_t total = console->fetch(page_state.console_consumed, &lines);
  page_state.console_consumed = total;
  if (lines.empty()) return;
  gui2_pages::append_console_lines(&page_state.console, ui, lines);
  if (scroll_to_end) gui2_pages::scroll_console_to_end(page_state.console);
}

static void show_console_page(page_transition transition) {
  create_page_scaffold(page_kind::CONSOLE, false, strings().console_title,
                       strings().console_summary, 0, transition);
  page_state.console_font_index =
      settings == nullptr ? 1 : std::clamp(settings->get_int("tw_gui2_console_font", 1), 0, 2);
  gui2_pages::console_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.empty_text = strings().console_empty;
  options.font = runtime_console_fonts[page_state.console_font_index];
  page_state.console = gui2_pages::build_console_page(options);
  page_state.console_consumed = 0;
  page_state.console_last_poll_ms = 0;
  poll_console(true);
}

static void poll_wipe_console(void) {
  if (console == nullptr || page_state.wipe_progress.console.body == nullptr) return;
  std::vector<gui2_backend::console_line> lines;
  const size_t total = console->fetch(page_state.wipe_console_consumed, &lines);
  page_state.wipe_console_consumed = total;
  if (lines.empty()) return;
  gui2_pages::append_console_lines(&page_state.wipe_progress.console, ui, lines);
  gui2_pages::scroll_console_to_end(page_state.wipe_progress.console);
}

static void show_wipe_progress_page(page_transition transition) {
  create_page_scaffold(page_kind::WIPE_PROGRESS, false, strings().wipe_title,
                       strings().wiping, 0, transition);
  page_state.console_font_index =
      settings == nullptr ? 1 : std::clamp(settings->get_int("tw_gui2_console_font", 1), 0, 2);

  gui2_pages::progress_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.console_font = runtime_console_fonts[page_state.console_font_index];
  options.initial_text = strings().wiping;
  options.subtitle = page_summary;
  page_state.wipe_progress = gui2_pages::build_progress_page(options);
  page_state.wipe_console_consumed = 0;
  page_state.wipe_last_poll_ms = 0;
  poll_wipe_console();
  refresh_wipe_progress();
}

static void refresh_wipe_progress(void) {
  if (wipe == nullptr) return;
  const auto status = wipe->status();

  gui2_pages::operation_status progress;
  progress.done = status.done;
  progress.total = status.total;
  switch (status.state) {
    case gui2_backend::wipe_state::DONE:
      progress.state = gui2_pages::operation_state::DONE;
      break;
    case gui2_backend::wipe_state::FAILED:
      progress.state = gui2_pages::operation_state::FAILED;
      break;
    default:
      progress.state = gui2_pages::operation_state::RUNNING;
      break;
  }

  gui2_pages::operation_labels labels;
  labels.running = strings().wiping;
  labels.done = strings().wipe_complete;
  labels.failed = strings().wipe_failed;
  gui2_pages::update_progress(&page_state.wipe_progress, labels, progress);
}

static void start_wipe_job(bool started) {
  if (!started) return;
  navigate_to(page_kind::WIPE_PROGRESS, nullptr, page_transition::PUSH);
}

static void factory_reset_confirmed(void*) {
  if (wipe == nullptr) return;
  start_wipe_job(wipe->start_factory_reset());
}

static void wipe_selection_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const auto* index = static_cast<const int*>(lv_event_get_user_data(event));
  lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (index == nullptr || toggle == nullptr) return;
  if (*index < 0 || static_cast<size_t>(*index) >= std::size(page_state.wipe_selected)) return;

  page_state.wipe_selected[*index] = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static std::vector<gui2_backend::wipe_target> wipe_targets;

static void advanced_wipe_confirmed(void*) {
  if (wipe == nullptr) return;
  std::vector<std::string> selected;
  for (size_t i = 0; i < wipe_targets.size() && i < std::size(page_state.wipe_selected); ++i) {
    if (page_state.wipe_selected[i]) selected.push_back(wipe_targets[i].mount_point);
  }
  if (selected.empty()) return;
  start_wipe_job(wipe->start_wipe(selected));
}

static void format_data_confirmed(void*) {
  if (wipe == nullptr) return;
  start_wipe_job(wipe->start_format_data());
}

static void show_wipe_page(page_transition transition) {
  const int bottom_reserved = ui.nav_height + gui2_pages::wipe_track_height() +
                              gui2_pages::wipe_hint_height(ui, strings().factory_reset_detail) +
                              ui.cards_top_gap * 3;
  create_page_scaffold(page_kind::WIPE, false, strings().wipe_title, strings().wipe_summary,
                       bottom_reserved, transition);

  gui2_pages::action_page_options action_options;
  action_options.content = main_content;
  action_options.metrics = &ui;
  action_options.strings = &strings();
  action_options.definition =
      &gui2_pages::action_definitions()[static_cast<int>(action_id::WIPE)];
  lv_obj_t* body = gui2_pages::build_action_page(action_options);
  if (body == nullptr) return;

  gui2_pages::wipe_page_options options;
  options.content = body;
  options.page_layer = page_layer;
  options.metrics = &ui;
  options.strings = &strings();
  options.option_event_callback = settings_option_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  options.advanced_target = &advanced_wipe_target;
  options.format_data_target = &format_data_target;
  options.has_data_media = wipe != nullptr && wipe->has_data_media();
  options.confirm = &page_state.wipe_confirm;
  options.confirm_callback = factory_reset_confirmed;
  gui2_pages::build_wipe_page(options);
}

static void show_advanced_wipe_page(page_transition transition) {
  const int track_height = gui2_pages::wipe_track_height();
  const int bottom_reserved = ui.nav_height + track_height + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::ADVANCED_WIPE, false, strings().advanced_wipe_title,
                       strings().advanced_wipe_summary, bottom_reserved, transition);

  wipe_targets = wipe == nullptr ? std::vector<gui2_backend::wipe_target>() : wipe->targets();
  for (bool& selected : page_state.wipe_selected) selected = false;
  page_state.wipe_target_count = std::min(wipe_targets.size(), std::size(page_state.wipe_selected));

  gui2_pages::advanced_wipe_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.targets = wipe_targets.data();
  options.target_count = page_state.wipe_target_count;
  options.selected = page_state.wipe_selected;
  options.option_event_callback = wipe_selection_event_cb;
  options.target_indices = wipe_target_indices;
  gui2_pages::build_advanced_wipe_page(options);

  const int page_height = ui.height - ui.status_height - ui.nav_height;
  page_state.wipe_confirm.create(page_layer, ui, ui.outer_margin,
                                 page_height - track_height - ui.cards_top_gap, ui.content_width,
                                 track_height, strings().swipe_wipe, advanced_wipe_confirmed,
                                 nullptr);
}

static bool format_data_ready(void) {
  lv_obj_t* input = page_state.format_data_input;
  if (input == nullptr) return false;
  const char* text = lv_textarea_get_text(input);
  return text != nullptr && std::string(text) == "yes";
}

static void format_data_input_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* track = page_state.format_data_track;
  if (track == nullptr) return;

  page_state.format_data_confirm.set_enabled(format_data_ready());
}

static void format_data_key_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_PRESSED) return;
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::KEYBOARD);
}

static void format_data_slide_confirmed(void*) {
  if (!format_data_ready()) {
    page_state.format_data_confirm.reset();
    return;
  }
  format_data_confirmed(nullptr);
}

static void decrypt_attempt(const std::string& password) {
  if (decrypt == nullptr || password.empty()) return;
  page_state.decrypt_refreshing = false;
  page_state.progress_settled_ms = 0;
  if (!decrypt->start(password)) return;
  navigate_to(page_kind::DECRYPT_PROGRESS, nullptr, page_transition::PUSH);
}

static void poll_decrypt_console(void) {
  if (console == nullptr || page_state.decrypt_progress.console.body == nullptr) return;
  std::vector<gui2_backend::console_line> lines;
  const size_t total = console->fetch(page_state.decrypt_console_consumed, &lines);
  page_state.decrypt_console_consumed = total;
  if (lines.empty()) return;
  gui2_pages::append_console_lines(&page_state.decrypt_progress.console, ui, lines);
  gui2_pages::scroll_console_to_end(page_state.decrypt_progress.console);
}

static void refresh_decrypt_progress(void) {
  if (decrypt == nullptr) return;
  const auto state = decrypt->state();

  gui2_pages::operation_status progress;
  progress.total = 0;  // the attempt cannot report how far along it is
  switch (state) {
    case gui2_backend::decrypt_state::DONE:
      progress.state = gui2_pages::operation_state::DONE;
      break;
    case gui2_backend::decrypt_state::FAILED:
      progress.state = gui2_pages::operation_state::FAILED;
      break;
    default:
      progress.state = gui2_pages::operation_state::RUNNING;
      break;
  }

  gui2_pages::operation_labels labels;
  labels.running =
      page_state.decrypt_refreshing ? strings().refreshing_sizes : strings().decrypting;
  labels.done =
      page_state.decrypt_refreshing ? strings().refresh_sizes_done : strings().decrypt_complete;
  labels.failed = strings().decrypt_failed;
  gui2_pages::update_progress(&page_state.decrypt_progress, labels, progress);

  if (state == gui2_backend::decrypt_state::RUNNING) {
    page_state.progress_settled_ms = 0;
    return;
  }

  // Let the bar finish filling and the colour land before the page changes;
  // jumping away the same frame reads as a flicker.
  const uint64_t now_ms = monotonic_ms();
  if (page_state.progress_settled_ms == 0) {
    page_state.progress_settled_ms = now_ms;
    if (hardware != nullptr)
      hardware->vibrate(state == gui2_backend::decrypt_state::DONE
                            ? gui2_backend::haptic_channel::ACTION
                            : gui2_backend::haptic_channel::BUTTON);
    return;
  }
  if (now_ms - page_state.progress_settled_ms < 900) return;

  const bool refreshing = page_state.decrypt_refreshing;
  decrypt->acknowledge();
  page_state.progress_settled_ms = 0;
  page_state.decrypt_refreshing = false;
  if (refreshing || state == gui2_backend::decrypt_state::DONE) {
    page_state.decrypt_failed = false;
    navigate_to(page_kind::HOME, nullptr, page_transition::REPLACE);
  } else {
    page_state.decrypt_failed = true;
    navigate_to(page_kind::DECRYPT, nullptr, page_transition::POP);
  }
}

static void show_decrypt_progress_page(page_transition transition) {
  create_page_scaffold(page_kind::DECRYPT_PROGRESS, false, strings().decrypt_title,
                       strings().decrypting, 0, transition);
  page_state.console_font_index =
      settings == nullptr ? 1 : std::clamp(settings->get_int("tw_gui2_console_font", 1), 0, 2);

  gui2_pages::progress_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.console_font = runtime_console_fonts[page_state.console_font_index];
  options.initial_text =
      page_state.decrypt_refreshing ? strings().refreshing_sizes : strings().decrypting;
  options.subtitle = page_summary;
  page_state.decrypt_progress = gui2_pages::build_progress_page(options);
  page_state.decrypt_console_consumed = 0;
  page_state.decrypt_last_poll_ms = 0;
  poll_decrypt_console();
  refresh_decrypt_progress();
}

static void decrypt_pattern_complete(const std::string& passphrase, void*) {
  decrypt_attempt(passphrase);
}

static void decrypt_pattern_dot(void*) {
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static void decrypt_input_ready_cb(lv_event_t*) {
  lv_obj_t* input = page_state.decrypt_input;
  if (input == nullptr) return;
  const char* text = lv_textarea_get_text(input);
  decrypt_attempt(text == nullptr ? std::string() : std::string(text));
}

static void decrypt_language_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  page_state.language_from_decrypt = true;
  navigate_to(page_kind::LANGUAGE);
}

static void home_notice_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  navigate_to(page_kind::DECRYPT);
}

static std::vector<gui2_backend::backup_target> backup_targets;
static std::vector<gui2_backend::mount_target> mount_targets;
static constexpr int kBackupCompressTarget = 100;
static constexpr int kBackupSkipDigestTarget = 101;
static constexpr int kBackupEncryptTarget = 103;
static constexpr int kMountSystemTarget = 102;

static void backup_selection_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const auto* index = static_cast<const int*>(lv_event_get_user_data(event));
  lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (index == nullptr || toggle == nullptr) return;
  if (*index < 0 || static_cast<size_t>(*index) >= std::size(page_state.backup_selected)) return;

  page_state.backup_selected[*index] = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static void backup_option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const auto* target = static_cast<const int*>(lv_event_get_user_data(event));
  lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == nullptr || toggle == nullptr) return;

  const bool checked = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  if (*target == kBackupCompressTarget)
    page_state.backup_compress = checked;
  else if (*target == kBackupSkipDigestTarget)
    page_state.backup_skip_digest = checked;
  else if (*target == kBackupEncryptTarget) {
    page_state.backup_encrypt = checked;
    gui2_pages::show_backup_password(page_state.backup_view, checked);
  }
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static void backup_tab_changed(size_t index, void*) {
  page_state.backup_active_tab = index;
  gui2_pages::show_backup_tab(page_state.backup_view, index);
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static void backup_confirmed(void*) {
  if (backup == nullptr) return;

  std::vector<std::string> selected;
  for (size_t i = 0; i < backup_targets.size() && i < std::size(page_state.backup_selected); ++i) {
    if (page_state.backup_selected[i]) selected.push_back(backup_targets[i].mount_point);
  }
  if (selected.empty()) {
    page_state.backup_confirm.reset();
    return;
  }

  std::string name;
  if (page_state.backup_view.name_input != nullptr) {
    const char* text = lv_textarea_get_text(page_state.backup_view.name_input);
    if (text != nullptr) name = text;
  }
  std::string password;
  if (page_state.backup_view.password_input != nullptr) {
    const char* text = lv_textarea_get_text(page_state.backup_view.password_input);
    if (text != nullptr) password = text;
  }
  if (!backup->start(selected, name, page_state.backup_compress, page_state.backup_skip_digest,
                     page_state.backup_encrypt, password)) {
    page_state.backup_confirm.reset();
    return;
  }
  navigate_to(page_kind::BACKUP_PROGRESS, nullptr, page_transition::PUSH);
}

static void mount_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const auto* index = static_cast<const int*>(lv_event_get_user_data(event));
  lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (index == nullptr || toggle == nullptr || mount == nullptr) return;

  const bool wanted = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  if (*index == kMountSystemTarget) {
    if (!mount->set_system_writable(wanted)) {
      if (wanted)
        lv_obj_remove_state(toggle, LV_STATE_CHECKED);
      else
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
    return;
  }

  if (*index < 0 || static_cast<size_t>(*index) >= mount_targets.size()) return;
  // The row only earns its new state once the partition actually moved.
  if (!mount->set_mounted(mount_targets[*index].mount_point, wanted)) {
    if (wanted)
      lv_obj_remove_state(toggle, LV_STATE_CHECKED);
    else
      lv_obj_add_state(toggle, LV_STATE_CHECKED);
  } else {
    mount_targets[*index].mounted = wanted;
  }
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static void show_backup_page(page_transition transition) {
  const int track_height = gui2_pages::wipe_track_height();
  const int bottom_reserved = ui.nav_height + track_height + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::BACKUP, false, strings().backup_title, strings().backup_summary,
                       bottom_reserved, transition);

  backup_targets = backup == nullptr ? std::vector<gui2_backend::backup_target>()
                                     : backup->targets();
  page_state.backup_target_count =
      std::min(backup_targets.size(), std::size(page_state.backup_selected));

  gui2_pages::action_page_options action_options;
  action_options.content = main_content;
  action_options.metrics = &ui;
  action_options.strings = &strings();
  action_options.definition =
      &gui2_pages::action_definitions()[static_cast<int>(action_id::BACKUP)];
  lv_obj_t* action_body = gui2_pages::build_action_page(action_options);
  if (action_body == nullptr) return;

  gui2_pages::backup_page_options options;
  options.content = action_body;
  options.page_layer = page_layer;
  options.overlay_layer = lv_layer_top();
  options.metrics = &ui;
  options.strings = &strings();
  options.targets = backup_targets.data();
  options.target_count = page_state.backup_target_count;
  options.selected = page_state.backup_selected;
  options.target_indices = wipe_target_indices;
  options.selection_callback = backup_selection_event_cb;
  options.compress = page_state.backup_compress;
  options.skip_digest = page_state.backup_skip_digest;
  options.compress_target = &kBackupCompressTarget;
  options.skip_digest_target = &kBackupSkipDigestTarget;
  options.encrypt = page_state.backup_encrypt;
  options.encrypt_target = &kBackupEncryptTarget;
  options.option_callback = backup_option_event_cb;
  options.keyboard_event_callback = format_data_key_event_cb;
  options.tabs = &page_state.backup_tabs;
  options.tab_callback = backup_tab_changed;
  options.active_tab = page_state.backup_active_tab;
  options.confirm = &page_state.backup_confirm;
  options.confirm_callback = backup_confirmed;
  page_state.backup_view = gui2_pages::build_backup_page(options);
}

static void poll_backup_console(void) {
  if (console == nullptr || page_state.backup_progress.console.body == nullptr) return;
  std::vector<gui2_backend::console_line> lines;
  const size_t total = console->fetch(page_state.backup_console_consumed, &lines);
  page_state.backup_console_consumed = total;
  if (lines.empty()) return;
  gui2_pages::append_console_lines(&page_state.backup_progress.console, ui, lines);
  gui2_pages::scroll_console_to_end(page_state.backup_progress.console);
}

static void refresh_backup_progress(void) {
  if (backup == nullptr) return;
  const auto status = backup->status();

  gui2_pages::operation_status progress;
  progress.total = 0;  // Run_Backup does not report step counts from outside
  switch (status.state) {
    case gui2_backend::backup_state::DONE:
      progress.state = gui2_pages::operation_state::DONE;
      break;
    case gui2_backend::backup_state::FAILED:
    case gui2_backend::backup_state::CANCELLED:
      progress.state = gui2_pages::operation_state::FAILED;
      break;
    default:
      progress.state = gui2_pages::operation_state::RUNNING;
      break;
  }

  gui2_pages::operation_labels labels;
  labels.running = status.detail.empty() ? strings().backing_up : status.detail.c_str();
  labels.done = strings().backup_complete;
  labels.failed = status.state == gui2_backend::backup_state::CANCELLED
                      ? strings().backup_cancelled
                      : strings().backup_failed;
  gui2_pages::update_progress(&page_state.backup_progress, labels, progress);
}

static void show_backup_progress_page(page_transition transition) {
  create_page_scaffold(page_kind::BACKUP_PROGRESS, false, strings().backup_title,
                       strings().backing_up, 0, transition);
  page_state.console_font_index =
      settings == nullptr ? 1 : std::clamp(settings->get_int("tw_gui2_console_font", 1), 0, 2);

  gui2_pages::progress_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.console_font = runtime_console_fonts[page_state.console_font_index];
  options.initial_text = strings().backing_up;
  options.subtitle = page_summary;
  page_state.backup_progress = gui2_pages::build_progress_page(options);
  page_state.backup_console_consumed = 0;
  page_state.backup_last_poll_ms = 0;
  poll_backup_console();
  refresh_backup_progress();
}

static void show_mount_page(page_transition transition) {
  create_page_scaffold(page_kind::MOUNT, false, strings().mount_title, strings().mount_summary, 0,
                       transition);

  mount_targets = mount == nullptr ? std::vector<gui2_backend::mount_target>() : mount->targets();
  page_state.mount_target_count =
      std::min(mount_targets.size(), std::size(wipe_target_indices));

  gui2_pages::action_page_options action_options;
  action_options.content = main_content;
  action_options.metrics = &ui;
  action_options.strings = &strings();
  action_options.definition =
      &gui2_pages::action_definitions()[static_cast<int>(action_id::MOUNT)];
  lv_obj_t* action_body = gui2_pages::build_action_page(action_options);
  if (action_body == nullptr) return;

  gui2_pages::mount_page_options options;
  options.content = action_body;
  options.metrics = &ui;
  options.strings = &strings();
  options.targets = mount_targets.data();
  options.target_count = page_state.mount_target_count;
  options.target_indices = wipe_target_indices;
  options.mount_callback = mount_event_cb;
  options.has_system = mount != nullptr;
  options.system_writable = mount != nullptr && mount->system_writable();
  options.system_target = &kMountSystemTarget;
  options.system_callback = mount_event_cb;
  gui2_pages::build_mount_page(options);
}

static void show_decrypt_page(page_transition transition) {
  const int bottom_reserved = ui.nav_height + single_line_card_height() + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::DECRYPT, false, strings().decrypt_title,
                       strings().decrypt_summary, bottom_reserved, transition);

  gui2_pages::decrypt_page_options options;
  options.content = main_content;
  options.page_layer = page_layer;
  options.overlay_layer = lv_layer_top();
  options.metrics = &ui;
  options.strings = &strings();
  options.kind = decrypt == nullptr ? gui2_backend::lock_kind::PASSWORD : decrypt->kind();
  options.failed = page_state.decrypt_failed;
  options.pattern = &page_state.decrypt_pattern;
  options.pattern_callback = decrypt_pattern_complete;
  options.pattern_dot_callback = decrypt_pattern_dot;
  options.input_ready_callback = decrypt_input_ready_cb;
  options.keyboard_event_callback = format_data_key_event_cb;
  options.language_callback = decrypt_language_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  const auto view = gui2_pages::build_decrypt_page(options);
  page_state.decrypt_input = view.input;
  page_state.decrypt_keyboard = view.keyboard;
  page_state.decrypt_status = view.status;
}

static void show_format_data_page(page_transition transition) {
  const int bottom_reserved =
      ui.nav_height + gui2_pages::wipe_track_height() + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::FORMAT_DATA, false, strings().format_data_title,
                       strings().format_data_summary, bottom_reserved, transition);

  gui2_pages::format_data_page_options options;
  options.content = main_content;
  options.page_layer = page_layer;
  options.metrics = &ui;
  options.strings = &strings();
  options.input_event_callback = format_data_input_event_cb;
  options.keyboard_event_callback = format_data_key_event_cb;
  options.overlay_layer = lv_layer_top();
  options.confirm = &page_state.format_data_confirm;
  options.confirm_callback = format_data_slide_confirmed;
  const auto view = gui2_pages::build_format_data_page(options);
  page_state.format_data_input = view.input;
  page_state.format_data_keyboard = view.keyboard;
  page_state.format_data_track = view.slider_track;
  page_state.format_data_confirm.set_enabled(false);
}

static constexpr int kKernelLogTarget = 0;
static constexpr int kLogcatTarget = 1;

static void log_option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const auto* target = static_cast<const int*>(lv_event_get_user_data(event));
  lv_obj_t* toggle = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == nullptr || toggle == nullptr) return;

  const bool checked = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  if (*target == kKernelLogTarget)
    page_state.include_kernel_log = checked;
  else
    page_state.include_logcat = checked;
  if (hardware != nullptr) hardware->vibrate(gui2_backend::haptic_channel::BUTTON);
}

static void export_log_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !accept_click(event)) return;
  if (page_state.export_result_label == nullptr) return;

  if (log_export == nullptr) {
    lv_label_set_text(page_state.export_result_label, strings().export_log_failed);
    lv_obj_set_style_text_color(page_state.export_result_label, lv_color_hex(0xF0443E),
                                LV_PART_MAIN);
    lv_obj_clear_flag(page_state.export_result_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const auto result = log_export->export_logs(page_state.include_kernel_log,
                                              page_state.include_logcat);
  if (result.success) {
    lv_label_set_text_fmt(page_state.export_result_label, "%s: %s", strings().export_log_done,
                          result.path.c_str());
    lv_obj_set_style_text_color(page_state.export_result_label, ui.secondary_text, LV_PART_MAIN);
  } else {
    lv_label_set_text(page_state.export_result_label, strings().export_log_failed);
    lv_obj_set_style_text_color(page_state.export_result_label, lv_color_hex(0xF0443E),
                                LV_PART_MAIN);
  }
  lv_obj_clear_flag(page_state.export_result_label, LV_OBJ_FLAG_HIDDEN);
}

static void show_timezone_page(page_transition transition) {
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
  const int bottom_reserved = ui.nav_height + button_height + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::TIMEZONE, false, strings().time_title, strings().time_summary,
                       bottom_reserved, transition);

  const std::string current = settings == nullptr
                                  ? "CST6CDT,M3.2.0,M11.1.0"
                                  : settings->get_string("tw_time_zone", "CST6CDT,M3.2.0,M11.1.0");
  std::string current_text = std::string(strings().current_timezone) + ": " + current;
  gui2_pages::timezone_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.timezone_indices = timezone_indices;
  options.offset_indices = offset_indices;
  options.format_indices = format_indices;
  options.current_timezone_text = current_text.c_str();
  options.timezone_event_callback = timezone_event_cb;
  options.offset_event_callback = offset_event_cb;
  options.format_event_callback = time_format_event_cb;
  options.dst_event_callback = dst_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  const auto view = gui2_pages::build_timezone_page(options);
  for (int i = 0; i < 24; ++i) timezone_cards[i] = view.timezone_cards[i];
  for (int i = 0; i < 4; ++i) offset_cards[i] = view.offset_cards[i];
  for (int i = 0; i < 2; ++i) format_cards[i] = view.format_cards[i];
  dst_card = view.dst_card;
  current_timezone_label = view.current_timezone_label;

  refresh_time_choices();
  gui2_components::create_apply_button(page_layer, ui, apply_timezone_event_cb, strings().apply,
                                       press_cancel_guard_cb);
}

static void show_home_page(page_transition transition) {
  create_page_scaffold(page_kind::HOME, true, "YARP", strings().home_summary, 0, transition);
  gui2_pages::home_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.actions = gui2_pages::action_definitions();
  options.action_count = gui2_pages::action_definition_count();
  options.action_event_callback = action_card_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  if (decrypt != nullptr && decrypt->is_encrypted()) {
    options.notice_text = strings().data_encrypted_notice;
    options.notice_event_callback = home_notice_event_cb;
  }
  gui2_pages::build_home_page(options);
}

static void show_action_page(const action_definition& definition, page_transition transition) {
  if (definition.id == action_id::WIPE) {
    show_wipe_page(transition);
    return;
  }
  if (definition.id == action_id::BACKUP) {
    show_backup_page(transition);
    return;
  }
  if (definition.id == action_id::MOUNT) {
    show_mount_page(transition);
    return;
  }

  const auto& action_text = strings().actions[static_cast<int>(definition.id)];
  create_page_scaffold(page_kind::ACTION, false, action_text.title, action_text.summary, 0,
                       transition);
  gui2_pages::action_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.definition = &definition;
  lv_obj_t* body = gui2_pages::build_action_page(options);
  if (body == nullptr) return;

  if (definition.id == action_id::SETTINGS) {
    gui2_pages::settings_page_options settings_options;
    settings_options.content = body;
    settings_options.metrics = &ui;
    settings_options.strings = &strings();
    settings_options.option_event_callback = settings_option_event_cb;
    settings_options.press_guard_callback = press_cancel_guard_cb;
    settings_options.language_target = &language_target;
    settings_options.timezone_target = &timezone_target;
    settings_options.screen_target = &brightness_target;
    settings_options.haptics_target = &haptics_target;
    settings_options.recording_target = &recording_target;
    settings_options.console_settings_target = &console_settings_target;
    settings_options.legacy_target = &legacy_target;
    settings_options.has_screen = true;
    settings_options.has_haptics = hardware != nullptr && hardware->has_haptics();
    settings_options.has_recording = screen != nullptr && screen->has_recording();
    gui2_pages::build_settings_page(settings_options);
  } else if (definition.id == action_id::ADVANCED) {
    gui2_pages::advanced_page_options advanced_options;
    advanced_options.content = body;
    advanced_options.metrics = &ui;
    advanced_options.strings = &strings();
    advanced_options.option_event_callback = settings_option_event_cb;
    advanced_options.press_guard_callback = press_cancel_guard_cb;
    advanced_options.export_log_target = &export_log_target;
    gui2_pages::build_advanced_page(advanced_options);
  }
}

static void show_export_log_page(page_transition transition) {
  const int bottom_reserved = ui.nav_height + single_line_card_height() + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::EXPORT_LOG, false, strings().export_log_title,
                       strings().export_log_summary, bottom_reserved, transition);

  const bool has_logcat = log_export != nullptr && log_export->has_logcat();
  if (!has_logcat) page_state.include_logcat = false;

  gui2_pages::export_log_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.option_event_callback = log_option_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  options.kernel_log_target = &kKernelLogTarget;
  options.logcat_target = &kLogcatTarget;
  options.include_kernel_log = page_state.include_kernel_log;
  options.include_logcat = page_state.include_logcat;
  options.has_logcat = has_logcat;
  const auto view = gui2_pages::build_export_log_page(options);
  page_state.kernel_log_card = view.kernel_log_card;
  page_state.logcat_card = view.logcat_card;
  page_state.export_result_label = view.result_label;

  gui2_components::create_apply_button(page_layer, ui, export_log_event_cb, strings().export_log,
                                       press_cancel_guard_cb);
}

static void show_language_page(page_transition transition) {
  pending_language = current_language;

  const int button_height = single_line_card_height();
  const int bottom_reserved = ui.nav_height + button_height + ui.cards_top_gap * 2;
  create_page_scaffold(page_kind::LANGUAGE, false, strings().language_title,
                       strings().language_summary, bottom_reserved, transition);

  gui2_pages::language_page_options options;
  options.content = main_content;
  options.metrics = &ui;
  options.strings = &strings();
  options.languages = language_values;
  options.language_count = 3;
  options.pending_language = pending_language;
  options.option_event_callback = language_option_event_cb;
  options.press_guard_callback = press_cancel_guard_cb;
  const auto view = gui2_pages::build_language_page(options);
  for (int i = 0; i < 3; ++i) {
    language_option_cards[i] = view.option_cards[i];
    language_check_labels[i] = view.check_labels[i];
  }

  gui2_components::create_apply_button(page_layer, ui, apply_language_event_cb, strings().apply,
                                       press_cancel_guard_cb);
}

static void route_page(const gui2_pages::page_request& request) {
  switch (request.id) {
    case page_kind::HOME:
      show_home_page(request.transition);
      return;
    case page_kind::ACTION:
      if (request.payload != nullptr)
        show_action_page(*static_cast<const action_definition*>(request.payload),
                         request.transition);
      return;
    case page_kind::REBOOT:
      show_reboot_page(request.transition);
      return;
    case page_kind::LANGUAGE:
      show_language_page(request.transition);
      return;
    case page_kind::TIMEZONE:
      show_timezone_page(request.transition);
      return;
    case page_kind::BRIGHTNESS:
      show_brightness_page(request.transition);
      return;
    case page_kind::HAPTICS:
      show_haptics_page(request.transition);
      return;
    case page_kind::RECORDING:
      show_recording_page(request.transition);
      return;
    case page_kind::CONSOLE:
      show_console_page(request.transition);
      return;
    case page_kind::EXPORT_LOG:
      show_export_log_page(request.transition);
      return;
    case page_kind::CONSOLE_SETTINGS:
      show_console_settings_page(request.transition);
      return;
    case page_kind::WIPE:
      show_wipe_page(request.transition);
      return;
    case page_kind::ADVANCED_WIPE:
      show_advanced_wipe_page(request.transition);
      return;
    case page_kind::FORMAT_DATA:
      show_format_data_page(request.transition);
      return;
    case page_kind::WIPE_PROGRESS:
      show_wipe_progress_page(request.transition);
      return;
    case page_kind::DECRYPT:
      show_decrypt_page(request.transition);
      return;
    case page_kind::DECRYPT_PROGRESS:
      show_decrypt_progress_page(request.transition);
      return;
    case page_kind::BACKUP:
      show_backup_page(request.transition);
      return;
    case page_kind::BACKUP_PROGRESS:
      show_backup_progress_page(request.transition);
      return;
    case page_kind::MOUNT:
      show_mount_page(request.transition);
      return;
  }
}

static void create_gui2_shell(lv_obj_t* screen) {
  gui2_shell::gui_shell_base_options options;
  options.screen = screen;
  options.text_font = runtime_text_font;
  options.status_font = runtime_status_font;
  options.brand_font = runtime_brand_font;
  options.recording_text = strings().recording_indicator;
  options.status_gesture_callback = status_gesture_event_cb;
  const auto base = gui2_shell::create_gui_shell_base(options);
  status_view = base.status;
  page_layer = base.page_layer;
  if (page_layer == nullptr) return;
  page_host.initialize(page_layer, ui);
  wheel_scroll_controller.initialize(ui, pointer_indev);

  navigate_to(page_kind::HOME, nullptr, page_transition::NONE);

  navigation_view = gui2_shell::create_bottom_navigation(
      screen, ui, home_navigation_active, navigation_event_cb, press_cancel_guard_cb);

  create_quick_menu();
  mouse_cursor = gui2_shell::create_mouse_cursor(pointer_indev, ev_has_mouse(), ui);
}

static void screen_lock_unlocked(void*) {
  screen_lock.hide();
}

static void screen_lock_before_screen_off(void*) {
  screen_lock.show();
}

static void shutdown_gui2(bool keep_display = false) {
  if (screen != nullptr && screen->is_recording()) screen->stop_recording();
  status_controller.stop();
  page_state.reboot.confirmation_slider.detach();
  page_host.clear();

  gui2_app::shutdown_graphics(&graphics, keep_display);
  runtime_text_font = nullptr;
  runtime_status_font = nullptr;
  runtime_brand_font = nullptr;
  for (lv_font_t*& font : runtime_console_fonts) font = nullptr;
  pointer_indev = nullptr;
  gui2_core::configure_click_guard(nullptr, nullptr);
  status_view = {};
  msg::SetTranslator(nullptr);
  page_layer = nullptr;
  main_content = nullptr;
  mouse_cursor = nullptr;
  quick_panel_view = {};
  quick_panel_controller = {};
  quick_record_button = nullptr;
  quick_record_label = nullptr;
  quick_feedback = nullptr;
  quick_brightness_binding = {};
  quick_brightness_dirty = false;
  screen_feedback.reset();
  screen_lock.reset();
  screen_actions.reset();
  wheel_scroll_controller.reset();
  screen = nullptr;
  reboot = nullptr;
  reboot_requested = false;
  ev_exit();
  if (!keep_display) gr_exit();
}

static bool gui2_loop_should_exit(void*) {
  return switch_to_legacy || reboot_requested;
}

static void gui2_loop_tick(void*, uint64_t now_ms) {
  advance_wheel_scroll(now_ms);
  screen_feedback.update(now_ms);
  if (page_state.console.body != nullptr && now_ms - page_state.console_last_poll_ms >= 100) {
    page_state.console_last_poll_ms = now_ms;
    poll_console(false);
  }
  if (page_state.wipe_progress.body != nullptr && now_ms - page_state.wipe_last_poll_ms >= 100) {
    page_state.wipe_last_poll_ms = now_ms;
    poll_wipe_console();
    refresh_wipe_progress();
  }
  if (page_state.decrypt_progress.body != nullptr &&
      now_ms - page_state.decrypt_last_poll_ms >= 100) {
    page_state.decrypt_last_poll_ms = now_ms;
    poll_decrypt_console();
    refresh_decrypt_progress();
  }
  if (page_state.backup_progress.body != nullptr &&
      now_ms - page_state.backup_last_poll_ms >= 100) {
    page_state.backup_last_poll_ms = now_ms;
    poll_backup_console();
    refresh_backup_progress();
  }
}

static void gui2_loop_key_action(void*, gui2_key_action key_action) {
  if (key_action == gui2_key_action::SCREENSHOT) {
    close_quick_menu();
    screen_actions.request_screenshot();
  } else if (key_action == gui2_key_action::BACK) {
    navigate_back();
  } else if (key_action == gui2_key_action::TOGGLE_SCREEN) {
    if (screen_actions.toggle_screen()) screen_lock.show();
  }
}

static void gui2_loop_activity(void*, bool was_screen_off) {
  if (was_screen_off) screen_lock.show();
}

static void gui2_loop_wheel(void*, int wheel) {
  if (main_content == nullptr || quick_menu_input_active() || pointer_indev == nullptr) return;
  lv_point_t point;
  lv_indev_get_point(pointer_indev, &point);
  lv_area_t content_area;
  lv_obj_get_coords(main_content, &content_area);
  const bool over_content = point.x >= content_area.x1 && point.x <= content_area.x2 &&
                            point.y >= content_area.y1 && point.y <= content_area.y2;
  if (over_content) queue_wheel_scroll(wheel);
}

static void gui2_loop_after_present(void*) {
  screen_actions.process_after_present();
}

int gui2_start(const gui2_context* context) {
  if (context == nullptr || context->settings == nullptr || context->hardware == nullptr ||
      context->screen == nullptr || context->reboot == nullptr)
    return GUI2_EXIT_INITIALIZATION_FAILED;

  settings = context->settings;
  hardware = context->hardware;
  screen = context->screen;
  reboot = context->reboot;
  console = context->console;
  log_export = context->log_export;
  wipe = context->wipe;
  decrypt = context->decrypt;
  backup = context->backup;
  mount = context->mount;
  current_language = language_from_code(settings->get_string("tw_language", "en"));
  pending_language = current_language;
  msg::SetTranslator(console_translator);
  switch_to_legacy = false;
  reboot_requested = false;
  screen_actions.reset();

  if (!gui2_app::initialize_graphics(context, &graphics, lv_tick_ms)) {
    shutdown_gui2();
    return GUI2_EXIT_INITIALIZATION_FAILED;
  }
  runtime_text_font = graphics.text_font;
  runtime_status_font = graphics.status_font;
  runtime_brand_font = graphics.brand_font;
  for (size_t i = 0; i < std::size(runtime_console_fonts); ++i)
    runtime_console_fonts[i] = graphics.console_fonts[i];
  pointer_indev = graphics.pointer_indev;
  gui2_core::configure_click_guard(pointer_indev, hardware);

  create_gui2_shell(lv_screen_active());
  screen_lock.create(ui, "", strings().swipe_to_unlock, screen_lock_unlocked, nullptr);
  screen_actions.initialize(screen, &screen_feedback, screen_lock_before_screen_off, nullptr);
  screen_actions.set_screenshot_result_callback(screenshot_result_cb);

  if (decrypt != nullptr && decrypt->is_encrypted())
    navigate_to(page_kind::DECRYPT, nullptr, page_transition::NONE);

  if (!status_controller.start(settings, ui, status_view, refresh_recording_ui)) {
    shutdown_gui2();
    return GUI2_EXIT_INITIALIZATION_FAILED;
  }

  gui2_app::loop_callbacks callbacks;
  callbacks.should_exit = gui2_loop_should_exit;
  callbacks.on_tick = gui2_loop_tick;
  callbacks.on_key_action = gui2_loop_key_action;
  callbacks.on_activity = gui2_loop_activity;
  callbacks.on_wheel = gui2_loop_wheel;
  callbacks.after_present = gui2_loop_after_present;
  gui2_app::run_gui2_loop(screen, pointer_indev, nullptr, callbacks);

  shutdown_gui2(switch_to_legacy);
  return switch_to_legacy ? GUI2_EXIT_TO_LEGACY : 0;
}
