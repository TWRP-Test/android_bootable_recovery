#ifndef GUI2_BACKEND_TWRP_SCREEN_BACKEND_H
#define GUI2_BACKEND_TWRP_SCREEN_BACKEND_H

#include <time.h>

#include <cstdint>
#include <string>

#include "screen_backend.h"
#include "settings_store.h"
#include "webm_vp8.h"

namespace gui2_backend {

class twrp_screen_backend final : public screen_backend {
 public:
  explicit twrp_screen_backend(settings_store* settings);
  ~twrp_screen_backend() override;

  bool has_screenshot() const override;
  capture_result save_screenshot() override;

  bool has_screen_off() const override;
  bool is_screen_off() const override;
  bool screen_off() override;
  bool screen_on() override;
  void set_before_screen_off_callback(void (*callback)(void*), void* user_data) override;
  void on_input_activity() override;
  void tick(uint64_t monotonic_ms) override;

  bool has_recording() const override;
  int max_recording_fps() const override;
  int recording_fps() const override;
  bool set_recording_fps(int fps) override;
  bool is_recording() const override;
  capture_result start_recording() override;
  capture_result stop_recording() override;
  void submit_frame(const frame_view& frame, uint64_t monotonic_ms) override;

 private:
  // blanktimer's states.
  enum { kOn = 0, kDim, kOff, kBlanked };

  bool has_brightness() const;
  std::string make_media_path(const char* directory, const char* prefix,
                              const char* extension) const;

  // blanktimer, with its lock overlay handed to the before-screen-off callback
  // and its forced render to the loop, which redraws once the screen is back.
  void setTimer();
  void checkForTimeout();
  std::string getBrightness() const;
  void resetTimerAndUnblank();
  void blank();

  settings_store* settings_;
  webm_vp8_recorder recorder_;
  int state = kOn;
  std::string orig_brightness;
  timespec btimer = {};
  uint64_t last_tick_ms_ = 0;
  void (*before_screen_off_callback_)(void*) = nullptr;
  void* before_screen_off_user_data_ = nullptr;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_SCREEN_BACKEND_H
