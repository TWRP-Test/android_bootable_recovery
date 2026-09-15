#include "shell/status_bar_controller.h"

namespace gui2_shell {

bool status_bar_controller::start(gui2_backend::settings_store* settings,
                                  const gui2_core::ui_metrics& metrics, const status_bar_view& view,
                                  void (*recording_refresh)()) {
  if (settings == nullptr) return false;
  metrics_ = metrics;
  view_ = view;
  recording_refresh_ = recording_refresh;
  provider_ = std::make_unique<gui2_backend::status_backend>(settings);
  provider_->start();
  timer_ = lv_timer_create(timer_callback, 1000, this);
  refresh();
  return true;
}

void status_bar_controller::refresh() {
  if (provider_ == nullptr) return;
  gui2_shell::update_status_bar(view_, metrics_, provider_->snapshot());
  if (recording_refresh_ != nullptr) recording_refresh_();
}

void status_bar_controller::timer_callback(lv_timer_t* timer) {
  auto* controller = static_cast<status_bar_controller*>(lv_timer_get_user_data(timer));
  if (controller != nullptr) controller->refresh();
}

void status_bar_controller::stop() {
  if (timer_ != nullptr) {
    lv_timer_del(timer_);
    timer_ = nullptr;
  }
  if (provider_ != nullptr) {
    provider_->stop();
    provider_.reset();
  }
  recording_refresh_ = nullptr;
  view_ = {};
}

}  // namespace gui2_shell
