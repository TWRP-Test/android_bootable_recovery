#ifndef GUI2_BACKEND_SCREEN_BACKEND_H
#define GUI2_BACKEND_SCREEN_BACKEND_H

#include <cstdint>
#include <string>

namespace gui2_backend {

// Frame description shared by capture and recording backends.
enum class frame_pixel_format {
  RGB565,
  RGBX8888,
  RGBA8888,
  BGRA8888,
  ABGR8888,
  ARGB8888,
  XRGB8888,
};

struct frame_view {
  const uint8_t* data = nullptr;
  int width = 0;
  int height = 0;
  int row_bytes = 0;
  frame_pixel_format format = frame_pixel_format::BGRA8888;
};

struct capture_result {
  bool success = false;
  std::string path;
  std::string error;
};

class screen_backend {
 public:
  virtual ~screen_backend() = default;

  virtual bool has_screenshot() const = 0;
  virtual capture_result save_screenshot() = 0;

  virtual bool has_screen_off() const = 0;
  virtual bool is_screen_off() const = 0;
  virtual bool screen_off() = 0;
  virtual bool screen_on() = 0;
  virtual void set_before_screen_off_callback(void (*callback)(void*), void* user_data) = 0;
  virtual void on_input_activity() = 0;
  virtual void tick(uint64_t monotonic_ms) = 0;

  virtual bool has_recording() const = 0;
  virtual int max_recording_fps() const = 0;
  virtual int recording_fps() const = 0;
  // Updates the setting without flushing it.
  virtual bool set_recording_fps(int fps) = 0;
  virtual bool is_recording() const = 0;
  virtual capture_result start_recording() = 0;
  virtual capture_result stop_recording() = 0;
  virtual void submit_frame(const frame_view& frame, uint64_t monotonic_ms) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_SCREEN_BACKEND_H
