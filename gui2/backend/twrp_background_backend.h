#ifndef GUI2_BACKEND_TWRP_BACKGROUND_BACKEND_H
#define GUI2_BACKEND_TWRP_BACKGROUND_BACKEND_H

#include "background_backend.h"

namespace gui2_backend {

class twrp_background_backend final : public background_backend {
 public:
  twrp_background_backend() = default;
  ~twrp_background_backend() override;

  twrp_background_backend(const twrp_background_backend&) = delete;
  twrp_background_backend& operator=(const twrp_background_backend&) = delete;

  void start() override;
  void poll() override;
  bool command_running() override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_BACKGROUND_BACKEND_H
