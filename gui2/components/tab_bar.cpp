#include "components/tab_bar.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

constexpr uint32_t kAccent = 0x347FF1;

struct pill_binding {
  tab_bar* owner;
  size_t index;
};

// One binding per pill, parked next to the bar so the pointers stay valid for
// as long as the objects do.
pill_binding bindings[tab_bar::kMaxTabs];

}  // namespace

lv_obj_t* tab_bar::create(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                          const char* const* labels, size_t count, size_t active,
                          change_callback callback, void* user_data) {
  if (parent == nullptr || labels == nullptr || count == 0) return nullptr;

  count_ = std::min(count, kMaxTabs);
  active_ = std::min(active, count_ - 1);
  callback_ = callback;
  user_data_ = user_data;

  const int height = gui2_core::single_line_card_height();
  const int padding = std::max(2, gui2_core::ui_px(6));

  root_ = lv_obj_create(parent);
  lv_obj_set_size(root_, metrics.content_width, height);
  gui2_core::set_surface_style(root_, metrics.card_color);
  lv_obj_set_style_radius(root_, height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_, padding, LV_PART_MAIN);
  lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(root_);

  const int inner_height = std::max(1, height - padding * 2);
  const int inner_width = std::max(1, metrics.content_width - padding * 2);
  const int pill_width = inner_width / static_cast<int>(count_);

  for (size_t i = 0; i < count_; ++i) {
    bindings[i] = { this, i };

    pills_[i] = lv_obj_create(root_);
    lv_obj_set_size(pills_[i], pill_width, inner_height);
    lv_obj_set_pos(pills_[i], static_cast<int>(i) * pill_width, 0);
    lv_obj_set_style_radius(pills_[i], inner_height / 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(pills_[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pills_[i], 0, LV_PART_MAIN);
    lv_obj_add_flag(pills_[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pills_[i], event_callback, LV_EVENT_CLICKED, &bindings[i]);
    gui2_core::disable_scrolling(pills_[i]);

    labels_[i] = lv_label_create(pills_[i]);
    lv_label_set_text(labels_[i], labels[i] == nullptr ? "" : labels[i]);
    lv_obj_set_style_text_font(labels_[i], metrics.text_font, LV_PART_MAIN);
    lv_obj_center(labels_[i]);
  }

  select(active_);
  return root_;
}

void tab_bar::select(size_t index) {
  if (index >= count_) return;
  active_ = index;
  for (size_t i = 0; i < count_; ++i) {
    if (pills_[i] == nullptr) continue;
    const bool on = i == index;
    lv_obj_set_style_bg_color(pills_[i], lv_color_hex(kAccent), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pills_[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    if (labels_[i] != nullptr)
      lv_obj_set_style_text_opa(labels_[i], on ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
  }
}

void tab_bar::detach() {
  root_ = nullptr;
  for (size_t i = 0; i < kMaxTabs; ++i) {
    pills_[i] = nullptr;
    labels_[i] = nullptr;
  }
  count_ = 0;
  active_ = 0;
  callback_ = nullptr;
  user_data_ = nullptr;
}

void tab_bar::event_callback(lv_event_t* event) {
  auto* binding = static_cast<pill_binding*>(lv_event_get_user_data(event));
  if (binding == nullptr || binding->owner == nullptr) return;
  tab_bar* self = binding->owner;
  if (binding->index == self->active_) return;

  self->select(binding->index);
  if (self->callback_ != nullptr) self->callback_(binding->index, self->user_data_);
}

}  // namespace gui2_components
