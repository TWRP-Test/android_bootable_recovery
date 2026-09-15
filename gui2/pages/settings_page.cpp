#include "pages/settings_page.h"

#include "components/setting_card.h"

namespace gui2_pages {

void build_settings_page(const settings_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  gui2_components::create_setting_card(options.content, metrics, strings.language_title,
                                       strings.current_language_detail,
                                       options.option_event_callback, options.language_target,
                                       options.press_guard_callback);
  gui2_components::create_setting_card(options.content, metrics, strings.time_title,
                                       strings.time_summary, options.option_event_callback,
                                       options.timezone_target, options.press_guard_callback);
  if (options.has_brightness) {
    gui2_components::create_setting_card(options.content, metrics, strings.brightness_title,
                                         strings.brightness_summary,
                                         options.option_event_callback, options.brightness_target,
                                         options.press_guard_callback);
  }
  if (options.has_haptics) {
    gui2_components::create_setting_card(options.content, metrics, strings.haptics_title,
                                         strings.haptics_summary, options.option_event_callback,
                                         options.haptics_target, options.press_guard_callback);
  }
  if (options.has_recording) {
    gui2_components::create_setting_card(
        options.content, metrics, strings.recording_settings_title,
        strings.recording_settings_summary, options.option_event_callback, options.recording_target,
        options.press_guard_callback);
  }
  gui2_components::create_setting_card(options.content, metrics, strings.classic_gui_title,
                                       strings.classic_gui_detail, options.option_event_callback,
                                       options.legacy_target, options.press_guard_callback);
}

}  // namespace gui2_pages
