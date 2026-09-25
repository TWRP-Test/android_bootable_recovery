#include "pages/reboot_page.h"

#include <algorithm>

#include "components/choice_card.h"
#include "components/icon_card.h"
#include "components/section_label.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;
constexpr uint32_t kDanger = 0xF0443E;

struct target_art {
  const lv_image_dsc_t* icon;
  uint32_t color;
};

// Powering off is the one row that does not lead anywhere else, so it is the
// only one drawn in the warning colour.
target_art art_for(gui2_backend::reboot_target target) {
  switch (target) {
    case gui2_backend::reboot_target::SYSTEM:
      return {&kGui2IconSystem, kAccent};
    case gui2_backend::reboot_target::RECOVERY:
      return {&kGui2IconAdvanced, kAccent};
    case gui2_backend::reboot_target::FASTBOOT:
      return {&kGui2IconWrench, kAccent};
    case gui2_backend::reboot_target::BOOTLOADER:
      return {&kGui2IconChip, kAccent};
    case gui2_backend::reboot_target::DOWNLOAD:
      return {&kGui2IconInstall, kAccent};
    case gui2_backend::reboot_target::EDL:
      return {&kGui2IconAdvanced, kAccent};
    case gui2_backend::reboot_target::POWER_OFF:
      return {&kGui2IconPower, kDanger};
  }
  return {&kGui2IconSystem, kAccent};
}

lv_obj_t* create_body(lv_obj_t* content, const gui2_core::ui_metrics& metrics) {
  lv_obj_t* body = lv_obj_create(content);
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  lv_obj_set_width(body, metrics.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap * 3 / 2, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(body);
  return body;
}

void mark_selected(lv_obj_t* card, const gui2_core::ui_metrics& metrics, bool selected) {
  lv_obj_set_style_border_width(card, selected ? gui2_core::ui_px(5) : 0, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      card, selected ? lv_color_mix(lv_color_hex(kAccent), metrics.card_color, 30)
                     : metrics.card_color,
      LV_PART_MAIN);
}

void style_choice_card(lv_obj_t* card, const gui2_core::ui_metrics& metrics, bool selected) {
  const lv_color_t color = selected ? lv_color_hex(kAccent) : metrics.card_color;
  lv_obj_set_style_bg_color(card, color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_hex(0xFFFFFF), color, 18),
                            LV_PART_MAIN | LV_STATE_PRESSED);
}

lv_obj_t* create_slot_row(lv_obj_t* body, const gui2_core::ui_metrics& metrics,
                          const reboot_page_options& options, int choice_height) {
  const int padding = gui2_core::ui_px(10);
  const int row_width = std::max(1, metrics.content_width - padding * 2);
  lv_obj_t* row = lv_obj_create(body);
  lv_obj_set_size(row, metrics.content_width, choice_height + padding * 2);
  gui2_core::set_surface_style(row, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(row, padding, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_overflow_visible(row, true);
  gui2_core::disable_scrolling(row);

  const int first_width = (row_width - metrics.card_gap) / 2;
  for (size_t i = 0; i < options.slot_count; ++i) {
    const int width = i == 0 ? first_width : row_width - first_width - metrics.card_gap;
    lv_obj_t* card = gui2_components::create_choice_card(
        row, metrics, i == 0 ? options.strings->boot_slot_a : options.strings->boot_slot_b, width,
        choice_height, options.slot_event_callback,
        const_cast<gui2_backend::boot_slot*>(&options.slots[i]), options.press_guard_callback);
    const std::string slot = i == 0 ? "A" : "B";
    style_choice_card(card, metrics,
                      options.active_slot != nullptr && *options.active_slot == slot);
  }
  return row;
}

}  // namespace

int reboot_track_height() {
  return std::clamp(gui2_core::ui_px(142), gui2_core::ui_px(112), gui2_core::ui_px(176));
}

reboot_page_view build_reboot_page(const reboot_page_options& options) {
  reboot_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.options == nullptr || options.option_event_callback == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  view.body = create_body(options.content, metrics);
  const int card_height = metrics.card_height;
  const size_t option_count = std::min(options.option_count, size_t(7));

  for (size_t i = 0; i < option_count; ++i) {
    const reboot_option& option = options.options[i];
    const target_art art = art_for(option.target);
    gui2_components::icon_card_options card;
    card.metrics = &metrics;
    card.icon = art.icon;
    card.icon_color = art.color;
    card.title = option.label;
    card.width = metrics.content_width;
    card.height = card_height;
    card.icon_size = metrics.icon_size;
    card.art_size = metrics.icon_size * 98 / 100;
    card.show_arrow = false;
    card.event_callback = options.option_event_callback;
    card.user_data = &option.target;
    card.press_guard_callback = options.press_guard_callback;
    lv_obj_t* object = gui2_components::create_icon_card(view.body, card);
    mark_selected(object, metrics,
                  options.target_selected && option.target == options.selected_target);
  }

  if (options.has_boot_slots && options.current_slot_text != nullptr && options.slots != nullptr &&
      options.slot_event_callback != nullptr) {
    gui2_components::create_section_label(view.body, metrics, options.current_slot_text);
    create_slot_row(view.body, metrics, options, gui2_core::single_line_card_height() * 7 / 6);
  }

  // The slider sits on the page rather than in the list, so it stays at the
  // bottom edge no matter how many targets the device offers.
  if (options.target_selected && options.confirmation_slider != nullptr &&
      options.confirmation_callback != nullptr && options.page_layer != nullptr) {
    const int track_height = reboot_track_height();
    const int page_height = metrics.height - metrics.status_height - metrics.nav_height;
    view.slider_track = options.confirmation_slider->create(
        options.page_layer, metrics, metrics.outer_margin,
        page_height - track_height - metrics.cards_top_gap, metrics.content_width, track_height,
        options.selected_target == gui2_backend::reboot_target::POWER_OFF
            ? options.strings->swipe_power_off
            : options.strings->swipe_reboot,
        options.confirmation_callback, options.confirmation_user_data);
  }

  if (options.error_text != nullptr && options.error_text[0] != '\0') {
    lv_obj_t* error = gui2_components::create_section_label(view.body, metrics, options.error_text);
    lv_obj_set_style_text_color(error, lv_color_hex(kDanger), LV_PART_MAIN);
  }

  return view;
}

}  // namespace gui2_pages
