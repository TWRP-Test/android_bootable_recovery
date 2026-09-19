#include "components/pattern_lock.h"

#include <algorithm>
#include <cstdlib>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

constexpr uint32_t kAccent = 0x347FF1;
constexpr uint32_t kIdle = 0x6A6A6A;

}  // namespace

lv_point_t pattern_lock::center_of(int index) const {
  const int row = index / kGrid;
  const int column = index % kGrid;
  return { static_cast<int32_t>(column * cell_ + cell_ / 2),
           static_cast<int32_t>(row * cell_ + cell_ / 2) };
}

lv_obj_t* pattern_lock::create(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int x, int y,
                               int size, pattern_complete_callback callback, void* user_data,
                               pattern_dot_callback dot_callback) {
  callback_ = callback;
  user_data_ = user_data;
  dot_callback_ = dot_callback;
  size_ = std::max(1, size);
  cell_ = std::max(1, size_ / kGrid);
  dot_size_ = std::clamp(cell_ / 5, gui2_core::ui_px(18), gui2_core::ui_px(34));
  count_ = 0;
  dragging_ = false;

  root_ = lv_obj_create(parent);
  lv_obj_set_size(root_, size_, size_);
  lv_obj_set_pos(root_, x, y);
  gui2_core::set_surface_style(root_, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(root_);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(root_, event_callback, LV_EVENT_ALL, this);

  path_ = lv_line_create(root_);
  lv_obj_set_size(path_, size_, size_);
  lv_obj_set_pos(path_, 0, 0);
  lv_obj_set_style_line_color(path_, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_line_width(path_, std::max(2, dot_size_ / 3), LV_PART_MAIN);
  lv_obj_set_style_line_rounded(path_, true, LV_PART_MAIN);
  lv_obj_remove_flag(path_, LV_OBJ_FLAG_CLICKABLE);

  for (int i = 0; i < kDots; ++i) {
    const lv_point_t center = center_of(i);
    dots_[i] = lv_obj_create(root_);
    lv_obj_set_size(dots_[i], dot_size_, dot_size_);
    lv_obj_set_pos(dots_[i], center.x - dot_size_ / 2, center.y - dot_size_ / 2);
    lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dots_[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dots_[i], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dots_[i], lv_color_hex(kIdle), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dots_[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(dots_[i], LV_OBJ_FLAG_CLICKABLE);
    gui2_core::disable_scrolling(dots_[i]);
  }
  return root_;
}

void pattern_lock::reset() {
  count_ = 0;
  dragging_ = false;
  for (int i = 0; i < kDots; ++i) {
    if (dots_[i] != nullptr) lv_obj_set_style_bg_color(dots_[i], lv_color_hex(kIdle), LV_PART_MAIN);
  }
  if (path_ != nullptr) lv_line_set_points(path_, points_, 0);
}

void pattern_lock::detach() {
  root_ = nullptr;
  path_ = nullptr;
  for (int i = 0; i < kDots; ++i) dots_[i] = nullptr;
  count_ = 0;
  dragging_ = false;
  callback_ = nullptr;
  dot_callback_ = nullptr;
  user_data_ = nullptr;
}

bool pattern_lock::local_point(lv_point_t* out) const {
  if (root_ == nullptr) return false;
  lv_indev_t* indev = lv_indev_active();
  if (indev == nullptr) return false;
  lv_point_t screen;
  lv_indev_get_point(indev, &screen);
  lv_area_t area;
  lv_obj_get_coords(root_, &area);
  out->x = screen.x - area.x1;
  out->y = screen.y - area.y1;
  return true;
}

int pattern_lock::dot_at(const lv_point_t& point) const {
  const int radius = std::max(dot_size_, cell_ * 2 / 5);
  for (int i = 0; i < kDots; ++i) {
    const lv_point_t center = center_of(i);
    const int dx = point.x - center.x;
    const int dy = point.y - center.y;
    if (dx * dx + dy * dy <= radius * radius) return i;
  }
  return -1;
}

bool pattern_lock::used(int index) const {
  for (int i = 0; i < count_; ++i) {
    if (order_[i] == index) return true;
  }
  return false;
}

void pattern_lock::connect(int index) {
  if (count_ >= kDots || used(index)) return;
  order_[count_++] = index;
  if (dots_[index] != nullptr)
    lv_obj_set_style_bg_color(dots_[index], lv_color_hex(kAccent), LV_PART_MAIN);
  if (dot_callback_ != nullptr) dot_callback_(user_data_);
}

// Android only auto-connects dots that sit on a horizontal, vertical or 45
// degree run between the two taps, and adds them in travel order.
void pattern_lock::connect_crossed(int index) {
  if (count_ == 0) return;

  const int previous = order_[count_ - 1];
  const int px = previous % kGrid;
  const int py = previous / kGrid;
  const int nx = index % kGrid;
  const int ny = index / kGrid;

  int step_x = (nx > px) ? 1 : -1;
  int step_y = (ny > py) ? 1 : -1;
  if (px == nx)
    step_x = 0;
  else if (py == ny)
    step_y = 0;
  else if (std::abs(px - nx) != std::abs(py - ny))
    return;

  int x = px;
  int y = py;
  while ((step_y == 0 || y != ny - step_y) && (step_x == 0 || x != nx - step_x)) {
    x += step_x;
    y += step_y;
    const int crossed = y * kGrid + x;
    if (!used(crossed)) connect(crossed);
  }
}

void pattern_lock::refresh_path(const lv_point_t* live) {
  if (path_ == nullptr) return;
  uint32_t total = 0;
  for (int i = 0; i < count_; ++i) {
    const lv_point_t center = center_of(order_[i]);
    points_[total].x = center.x;
    points_[total].y = center.y;
    ++total;
  }
  if (live != nullptr && count_ > 0) {
    points_[total].x = live->x;
    points_[total].y = live->y;
    ++total;
  }
  lv_line_set_points(path_, points_, total < 2 ? 0 : total);
}

void pattern_lock::finish() {
  dragging_ = false;
  if (count_ == 0) {
    refresh_path(nullptr);
    return;
  }

  std::string passphrase;
  passphrase.reserve(count_);
  for (int i = 0; i < count_; ++i) {
    passphrase.push_back(static_cast<char>((order_[i] & 0xff) + '1'));
  }
  refresh_path(nullptr);

  pattern_complete_callback callback = callback_;
  void* user_data = user_data_;
  if (callback != nullptr) callback(passphrase, user_data);
}

void pattern_lock::event_callback(lv_event_t* event) {
  auto* self = static_cast<pattern_lock*>(lv_event_get_user_data(event));
  if (self == nullptr || self->root_ == nullptr) return;

  const lv_event_code_t code = lv_event_get_code(event);
  lv_point_t point;

  switch (code) {
    case LV_EVENT_PRESSED: {
      self->reset();
      if (!self->local_point(&point)) return;
      self->dragging_ = true;
      const int index = self->dot_at(point);
      if (index >= 0) self->connect(index);
      self->refresh_path(&point);
      break;
    }
    case LV_EVENT_PRESSING: {
      if (!self->dragging_ || !self->local_point(&point)) return;
      const int index = self->dot_at(point);
      if (index >= 0 && !self->used(index)) {
        self->connect_crossed(index);
        self->connect(index);
      }
      self->refresh_path(&point);
      break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      if (self->dragging_) self->finish();
      break;
    default:
      break;
  }
}

}  // namespace gui2_components
