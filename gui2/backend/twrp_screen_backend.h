#ifndef GUI2_BACKEND_TWRP_SCREEN_BACKEND_H
#define GUI2_BACKEND_TWRP_SCREEN_BACKEND_H

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
  enum class screen_state {
    ON,
    DIM,
    OFF,
    BLANKED,
  };

  bool has_brightness() const;
  std::string current_brightness() const;
  std::string make_media_path(const char* directory, const char* prefix,
                              const char* extension) const;
  capture_result stop_recording_locked();
  void blank_locked();
  void unblank_locked();

  settings_store* settings_;
  webm_vp8_recorder recorder_;
  screen_state state_ = screen_state::ON;
  std::string original_brightness_;
  uint64_t last_activity_ms_ = 0;
  uint64_t last_tick_ms_ = 0;
  uint64_t dim_start_ms_ = 0;
  int last_dim_brightness_ = -1;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_SCREEN_BACKEND_H
