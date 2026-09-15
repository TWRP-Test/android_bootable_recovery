#ifndef GUI2_SHELL_STATUS_BAR_CONTROLLER_H
#define GUI2_SHELL_STATUS_BAR_CONTROLLER_H

#include <memory>

#include "backend/settings_store.h"
#include "backend/status_backend.h"
#include "core/ui_metrics.h"
#include "shell/status_bar.h"

namespace gui2_shell {

class status_bar_controller {
 public:
  bool start(gui2_backend::settings_store* settings, const gui2_core::ui_metrics& metrics,
             const status_bar_view& view, void (*recording_refresh)());
  void refresh();
  void stop();

 private:
  static void timer_callback(lv_timer_t* timer);

  gui2_core::ui_metrics metrics_;
  status_bar_view view_;
  void (*recording_refresh_)() = nullptr;
  std::unique_ptr<gui2_backend::status_backend> provider_;
  lv_timer_t* timer_ = nullptr;
};

}  // namespace gui2_shell

#endif
