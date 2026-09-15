#include "shell/bottom_navigation.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"
#include "gui2_svg_cache.h"

namespace gui2_shell {

namespace {

constexpr navigation_action kBackAction = navigation_action::BACK;
constexpr navigation_action kHomeAction = navigation_action::HOME;
constexpr navigation_action kLogAction = navigation_action::LOG;
constexpr navigation_action kPowerAction = navigation_action::POWER;

lv_obj_t* create_button(lv_obj_t* parent, const lv_image_dsc_t* source, int size,
                        const gui2_core::ui_metrics& metrics, const navigation_action& action,
                        lv_event_cb_t event_callback, lv_event_cb_t press_guard_callback,
                        int width = -1, int height = -1, bool embedded = false) {
  lv_obj_t* button = lv_obj_create(parent);
  const int button_width = width > 0 ? width : size;
  const int button_height = height > 0 ? height : size;
  lv_obj_set_size(button, button_width, button_height);
  gui2_core::disable_scrolling(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  if (embedded) {
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  } else {
    const int radius_size = std::min(button_width, button_height);
    lv_obj_set_style_radius(button, radius_size / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, metrics.nav_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.nav_color, 18),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, gui2_core::ui_px(4), LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x3B3B3B), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  }
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(button, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (event_callback != nullptr)
    lv_obj_add_event_cb(button, event_callback, LV_EVENT_CLICKED,
                        const_cast<navigation_action*>(&action));

  const int icon_size =
      std::clamp(button_height * 58 / 100, gui2_core::ui_px(52), gui2_core::ui_px(84));
  lv_obj_t* image = gui2_components::create_svg_image(button, source, icon_size, icon_size);
  lv_obj_center(image);
  return button;
}

}  // namespace

bottom_navigation_view create_bottom_navigation(lv_obj_t* screen,
                                                const gui2_core::ui_metrics& metrics,
                                                bool home_active, lv_event_cb_t event_callback,
                                                lv_event_cb_t press_guard_callback) {
  bottom_navigation_view view;
  if (screen == nullptr) return view;

  const bool landscape = metrics.width > metrics.height;
  const int navigation_control_size =
      std::clamp(metrics.nav_height * 72 / 100, gui2_core::ui_px(108), gui2_core::ui_px(148));
  const int pill_height = navigation_control_size;
  const int pill_width = std::clamp(landscape ? metrics.width * 44 / 100 : metrics.width * 64 / 100,
                                    gui2_core::ui_px(270), gui2_core::ui_px(700));
  const int navigation_gap =
      std::clamp(gui2_core::ui_px(30), gui2_core::ui_px(18), gui2_core::ui_px(30));
  const int group_width = navigation_control_size + navigation_gap + pill_width;
  const int group_left = std::max(0, (metrics.width - group_width) / 2);
  const int bottom_padding =
      std::clamp(gui2_core::ui_px(22), gui2_core::ui_px(12), gui2_core::ui_px(22));
  const int content_height = std::max(1, metrics.nav_height - bottom_padding);
  const int group_top = std::max(0, (content_height - navigation_control_size) / 2);
  const int pill_top = std::max(0, (content_height - pill_height) / 2);

  view.root = lv_obj_create(screen);
  lv_obj_set_pos(view.root, 0, metrics.height - metrics.nav_height);
  lv_obj_set_size(view.root, metrics.width, metrics.nav_height);
  gui2_core::set_surface_style(view.root, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.root, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.root);

  lv_obj_t* back = create_button(view.root, &kGui2IconBack, navigation_control_size, metrics,
                                 kBackAction, event_callback, press_guard_callback);
  lv_obj_set_pos(back, group_left, group_top);

  lv_obj_t* pill = lv_obj_create(view.root);
  lv_obj_set_size(pill, pill_width, pill_height);
  lv_obj_set_pos(pill, group_left + navigation_control_size + navigation_gap, pill_top);
  gui2_core::set_surface_style(pill, metrics.nav_color);
  lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(pill, pill_height / 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(pill, gui2_core::ui_px(4), LV_PART_MAIN);
  lv_obj_set_style_border_color(pill, lv_color_hex(0x3B3B3B), LV_PART_MAIN);
  lv_obj_set_style_border_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(pill, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(pill, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(pill, gui2_core::ui_px(3), LV_PART_MAIN);
  lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(pill);

  const int item_width = pill_width / 3;
  const int icon_size = std::min(pill_height - gui2_core::ui_px(12), gui2_core::ui_px(108));
  lv_obj_t* home = create_button(pill, home_active ? &kGui2IconHomeFilled : &kGui2IconHomeOutlined,
                                 icon_size, metrics, kHomeAction, event_callback,
                                 press_guard_callback, item_width, pill_height, true);
  lv_obj_t* console =
      create_button(pill, &kGui2IconConsoleOutline, icon_size, metrics, kLogAction, event_callback,
                    press_guard_callback, item_width, pill_height, true);
  lv_obj_t* power =
      create_button(pill, &kGui2IconPower, icon_size, metrics, kPowerAction, event_callback,
                    press_guard_callback, pill_width - item_width * 2, pill_height, true);
  view.home_icon = lv_obj_get_child(home, 0);
  view.console_icon = lv_obj_get_child(console, 0);
  view.power_icon = lv_obj_get_child(power, 0);
  return view;
}

void refresh_bottom_navigation(const bottom_navigation_view& view, bool home_active) {
  if (view.home_icon != nullptr)
    lv_image_set_src(
        view.home_icon,
        gui2_svg_get_raster(home_active ? &kGui2IconHomeFilled : &kGui2IconHomeOutlined,
                            lv_obj_get_width(view.home_icon), lv_obj_get_height(view.home_icon)));
  if (view.console_icon != nullptr)
    lv_image_set_src(view.console_icon, gui2_svg_get_raster(&kGui2IconConsoleOutline,
                                                            lv_obj_get_width(view.console_icon),
                                                            lv_obj_get_height(view.console_icon)));
  if (view.power_icon != nullptr)
    lv_image_set_src(view.power_icon,
                     gui2_svg_get_raster(&kGui2IconPower, lv_obj_get_width(view.power_icon),
                                         lv_obj_get_height(view.power_icon)));
}

}  // namespace gui2_shell
