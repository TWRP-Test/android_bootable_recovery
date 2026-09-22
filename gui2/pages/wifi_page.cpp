#include "pages/wifi_page.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "components/check_row.h"
#include "components/section_label.h"
#include "components/setting_card.h"
#include "components/tip_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;
constexpr uint32_t kAccentSurface = 0x0E1B2E;
constexpr uint32_t kWarning = 0xF0443E;
constexpr uint32_t kWarningSurface = 0x2A1010;

const char* security_label(const gui2_i18n::language_pack& strings,
                           gui2_backend::wifi_security security) {
  switch (security) {
    case gui2_backend::wifi_security::WPA3:
      return "WPA3";
    case gui2_backend::wifi_security::WPA2:
      return "WPA2";
    case gui2_backend::wifi_security::WPA:
      return "WPA";
    case gui2_backend::wifi_security::OPEN:
      break;
  }
  return strings.wifi_open;
}

lv_obj_t* create_column(lv_obj_t* parent, const gui2_core::ui_metrics& metrics) {
  lv_obj_t* column = lv_obj_create(parent);
  lv_obj_set_pos(column, metrics.outer_margin, 0);
  lv_obj_set_width(column, metrics.content_width);
  lv_obj_set_height(column, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(column, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(column, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(column, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(column, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(column);
  return column;
}

lv_obj_t* create_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int width,
                        const char* text, bool primary, lv_event_cb_t callback,
                        lv_event_cb_t press_guard_callback) {
  const int height = gui2_core::single_line_card_height();
  const lv_color_t fill = primary ? lv_color_hex(kAccent) : metrics.card_color;
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::set_surface_style(button, fill);
  lv_obj_set_style_radius(button, height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), fill, 18),
                            LV_STATE_PRESSED);
  gui2_core::disable_scrolling(button);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(button, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (callback != nullptr) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(label, primary ? lv_color_hex(0xFFFFFF) : lv_color_hex(kAccent),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

}  // namespace

wifi_page_view build_wifi_page(const wifi_page_options& options) {
  wifi_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  lv_obj_t* body = create_column(options.content, metrics);

  view.service_toggle = gui2_components::create_switch_row(
      body, metrics, strings.wifi_service, options.service_running, options.service_callback,
      nullptr);

  if (options.service_running) {
    // Scan leads, so it gets the filled button; the other two report.
    const int gap = metrics.card_gap;
    lv_obj_t* row = lv_obj_create(body);
    lv_obj_set_size(row, metrics.content_width, LV_SIZE_CONTENT);
    gui2_core::set_surface_style(row, metrics.background, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, gap, LV_PART_MAIN);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    gui2_core::disable_scrolling(row);
    const int width = std::max(1, (metrics.content_width - gap * 2) / 3);
    create_button(row, metrics, width, strings.wifi_scan, true, options.scan_callback,
                  options.press_guard_callback);
    create_button(row, metrics, width, strings.wifi_status, false, options.status_callback,
                  options.press_guard_callback);
    create_button(row, metrics, width, strings.wifi_test, false, options.test_callback,
                  options.press_guard_callback);

    if (options.failed_text != nullptr)
      gui2_components::create_tip_card(body, metrics, options.failed_text,
                                       lv_color_hex(kWarning), lv_color_hex(kWarningSurface));
    if (options.connected_ssid != nullptr && options.connected_ssid[0] != '\0') {
      const std::string text = std::string(strings.wifi_connected) + ": " + options.connected_ssid;
      gui2_components::create_tip_card(body, metrics, text.c_str(), lv_color_hex(kAccent),
                                       lv_color_hex(kAccentSurface));
    }

    gui2_components::create_section_label(body, metrics, strings.wifi_networks);
    if (options.busy_text != nullptr) {
      gui2_components::create_tip_card(body, metrics, options.busy_text, metrics.secondary_text,
                                       metrics.card_color);
    } else if (options.network_count == 0 || options.networks == nullptr) {
      gui2_components::create_tip_card(body, metrics, strings.wifi_none, metrics.secondary_text,
                                       metrics.card_color);
    } else {
      for (size_t i = 0; i < options.network_count; ++i) {
        const gui2_backend::wifi_network& network = options.networks[i];
        char detail[64];
        std::snprintf(detail, sizeof(detail), "%s · %d dBm",
                      security_label(strings, network.security), network.signal_dbm);
        gui2_components::create_setting_card(
            body, metrics, network.ssid.c_str(), detail, options.network_callback,
            options.network_indices == nullptr ? nullptr : &options.network_indices[i],
            options.press_guard_callback);
      }
    }
  }

  // The legacy page is mostly this box; here it sits under the list and
  // scrolls on its own.
  gui2_components::create_section_label(body, metrics, strings.wifi_log);
  console_page_options log;
  log.content = body;
  log.metrics = &metrics;
  log.empty_text = "";
  log.font = options.log_font;
  log.self_scrolling = true;
  view.log = build_console_page(log);
  if (view.log.body != nullptr) {
    const int height = gui2_core::ui_px(640);
    lv_obj_set_height(view.log.body, height);
    view.log.minimum_height = height;
  }
  return view;
}

}  // namespace gui2_pages
