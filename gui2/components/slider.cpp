#include "slider.h"

#include <algorithm>
#include <cstdlib>

#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_private.h"

namespace gui2_components {

struct slider_instance {
  lv_obj_t object;
  int32_t minimum;
  int32_t maximum;
  int32_t value;
  lv_color_t foreground;
  lv_color_t thumb;
  bool dragging;
  bool enabled;
  int grab_offset;
};

static void slider_constructor(const lv_obj_class_t* class_p, lv_obj_t* object);
static void slider_event(const lv_obj_class_t* class_p, lv_event_t* event);

static const lv_obj_class_t slider_class = { .constructor_cb = slider_constructor,
                                             .destructor_cb = nullptr,
                                             .event_cb = slider_event,
                                             .width_def = LV_DPI_DEF * 2,
                                             .height_def = 28,
                                             .editable = LV_OBJ_CLASS_EDITABLE_TRUE,
                                             .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
                                             .instance_size = sizeof(slider_instance),
                                             .base_class = &lv_obj_class,
                                             .name = "gui2_slider",
                                             LV_PROPERTY_CLASS_FIELDS(slider, GUI2_SLIDER) };

static slider_instance* instance(lv_obj_t* object) {
  return reinterpret_cast<slider_instance*>(object);
}

static const slider_instance* instance(const lv_obj_t* object) {
  return reinterpret_cast<const slider_instance*>(object);
}

static int value_from_point(const slider_instance* slider, lv_point_t point) {
  lv_area_t area;
  lv_obj_get_coords(&slider->object, &area);
  const int width = lv_area_get_width(&area);
  const int height = lv_area_get_height(&area);
  const int travel = std::max(0, width - height);
  if (travel == 0) return slider->minimum;

  const int local_x = std::clamp(point.x - area.x1, 0, width - 1);
  const int position = std::clamp(local_x - height / 2, 0, travel);
  const int range = std::max(1, slider->maximum - slider->minimum);
  return slider->minimum + (position * range + travel / 2) / travel;
}

static void set_value_internal(slider_instance* slider, int value, bool send_event) {
  value = std::clamp(value, static_cast<int>(slider->minimum), static_cast<int>(slider->maximum));
  if (slider->value == value) return;

  slider->value = value;
  lv_obj_invalidate(&slider->object);
  if (send_event) lv_obj_send_event(&slider->object, LV_EVENT_VALUE_CHANGED, nullptr);
}

static int knob_center_x(const slider_instance* slider) {
  lv_area_t area;
  lv_obj_get_coords(&slider->object, &area);
  const int width = lv_area_get_width(&area);
  const int height = lv_area_get_height(&area);
  const int travel = std::max(0, width - height);
  const int range = std::max(1, slider->maximum - slider->minimum);
  const int position = (travel * (slider->value - slider->minimum) + range / 2) / range;
  return area.x1 + height / 2 + position;
}

static bool pointer_position(const slider_instance* slider, lv_point_t* point) {
  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return false;

  lv_indev_get_point(indev, point);
  lv_obj_transform_point(&slider->object, point, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE);
  return true;
}

static bool begin_drag(slider_instance* slider) {
  slider->grab_offset = 0;
  if (!slider->enabled) return false;
  lv_point_t point;
  if (!pointer_position(slider, &point)) return false;

  lv_area_t area;
  lv_obj_get_coords(&slider->object, &area);
  const int grab_radius = std::max<int>(1, lv_area_get_height(&area));
  const int offset = point.x - knob_center_x(slider);
  if (std::abs(offset) > grab_radius) return false;

  slider->grab_offset = offset;
  return true;
}

static void update_from_pointer(slider_instance* slider) {
  lv_point_t point;
  if (!pointer_position(slider, &point)) return;

  point.x -= slider->grab_offset;
  set_value_internal(slider, value_from_point(slider, point), true);
}

static void draw_rect(lv_layer_t* layer, lv_obj_t* object, lv_part_t part, lv_area_t area,
                      lv_color_t color, lv_opa_t opa) {
  lv_draw_rect_dsc_t draw;
  lv_draw_rect_dsc_init(&draw);
  lv_obj_init_draw_rect_dsc(object, part, &draw);
  draw.bg_color = color;
  draw.bg_opa = opa;
  draw.radius = LV_RADIUS_CIRCLE;
  draw.border_width = 0;
  draw.shadow_width = 0;
  lv_draw_rect(layer, &draw, &area);
}

static void draw_slider(slider_instance* slider, lv_event_t* event) {
  lv_layer_t* layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(&slider->object, &area);
  const int width = lv_area_get_width(&area);
  const int height = lv_area_get_height(&area);
  const int range = std::max(1, slider->maximum - slider->minimum);
  const int travel = std::max(0, width - height);
  const int position = (travel * (slider->value - slider->minimum) + range / 2) / range;
  const int center = height / 2 + position;
  const int fill_width = std::clamp(center + height / 2, 1, width);
  const bool active =
      slider->enabled && (slider->dragging || lv_obj_has_state(&slider->object, LV_STATE_PRESSED));
  const int knob_size = std::max(1, (height * 72 * (active ? 1127 : 1000) + 50000) / 100000);
  const lv_opa_t part_opa = slider->enabled ? LV_OPA_COVER : LV_OPA_40;

  lv_area_t fill_area = area;
  fill_area.x2 = fill_area.x1 + fill_width - 1;
  draw_rect(layer, &slider->object, LV_PART_INDICATOR, fill_area, slider->foreground, part_opa);

  if (active) draw_rect(layer, &slider->object, LV_PART_MAIN, area, lv_color_black(), 11);

  lv_area_t knob_area;
  knob_area.x1 = area.x1 + center - knob_size / 2;
  knob_area.y1 = area.y1 + (height - knob_size) / 2;
  knob_area.x2 = knob_area.x1 + knob_size - 1;
  knob_area.y2 = knob_area.y1 + knob_size - 1;
  draw_rect(layer, &slider->object, LV_PART_KNOB, knob_area, slider->thumb, part_opa);
}

static void slider_constructor(const lv_obj_class_t* class_p, lv_obj_t* object) {
  auto* slider = instance(object);
  slider->minimum = 0;
  slider->maximum = 100;
  slider->value = 0;
  slider->foreground = lv_color_hex(0x347FF1);
  slider->thumb = lv_color_hex(0xFFFFFF);
  slider->dragging = false;
  slider->enabled = true;
  slider->grab_offset = 0;
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_flag(object, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_ext_click_area(object, 8);
  (void)class_p;
}

static void slider_event(const lv_obj_class_t* class_p, lv_event_t* event) {
  if (lv_obj_event_base(class_p, event) != LV_RESULT_OK) return;

  auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  auto* slider = instance(object);
  switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
      slider->dragging = begin_drag(slider);
      lv_obj_invalidate(object);
      break;
    case LV_EVENT_PRESSING:
      if (slider->dragging) update_from_pointer(slider);
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      slider->dragging = false;
      slider->grab_offset = 0;
      lv_obj_invalidate(object);
      break;
    case LV_EVENT_KEY: {
      const uint32_t key = lv_event_get_key(event);
      if (key == LV_KEY_LEFT || key == LV_KEY_DOWN)
        set_value_internal(slider, slider->value - 1, true);
      if (key == LV_KEY_RIGHT || key == LV_KEY_UP)
        set_value_internal(slider, slider->value + 1, true);
      break;
    }
    case LV_EVENT_STATE_CHANGED:
    case LV_EVENT_SIZE_CHANGED:
      lv_obj_invalidate(object);
      break;
    case LV_EVENT_DRAW_MAIN:
      draw_slider(slider, event);
      break;
    default:
      break;
  }
}

lv_obj_t* create_slider(lv_obj_t* parent, int x, int y, int width, int height, int minimum,
                        int maximum, int value, lv_color_t background, lv_color_t foreground,
                        lv_color_t thumb, slider* component) {
  if (component == nullptr) return nullptr;

  auto* object = lv_obj_class_create_obj(&slider_class, parent);
  if (object == nullptr) return nullptr;
  lv_obj_class_init_obj(object);
  auto* slider = instance(object);
  slider->minimum = minimum;
  slider->maximum = std::max(minimum, maximum);
  slider->value = std::clamp(value, minimum, slider->maximum);
  slider->foreground = foreground;
  slider->thumb = thumb;
  lv_obj_set_size(object, width, height);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_style_bg_color(object, background, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, foreground, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(object, thumb, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_KNOB);
  component->object = object;
  lv_obj_invalidate(object);
  return object;
}

int get_value(const slider* component) {
  return component == nullptr || component->object == nullptr ? 0
                                                              : instance(component->object)->value;
}

void set_enabled(slider* component, bool enabled) {
  if (component == nullptr || component->object == nullptr) return;
  auto* slider = instance(component->object);
  if (slider->enabled == enabled) return;
  slider->enabled = enabled;
  if (!enabled) {
    slider->dragging = false;
    slider->grab_offset = 0;
  }
  lv_obj_invalidate(component->object);
}

void set_value(slider* component, int value) {
  if (component == nullptr || component->object == nullptr) return;
  set_value_internal(instance(component->object), value, false);
}

void refresh_slider(slider* component) {
  if (component != nullptr && component->object != nullptr) lv_obj_invalidate(component->object);
}

}  // namespace gui2_components
