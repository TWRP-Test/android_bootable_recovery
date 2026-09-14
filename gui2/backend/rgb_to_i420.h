#ifndef GUI2_BACKEND_RGB_TO_I420_H
#define GUI2_BACKEND_RGB_TO_I420_H

#include <cstdint>
#include <vector>

#include "screen_backend.h"

namespace gui2_backend {

struct i420_frame {
  int width = 0;
  int height = 0;
  int chroma_width = 0;
  int chroma_height = 0;
  int y_stride = 0;
  int u_stride = 0;
  int v_stride = 0;
  std::vector<uint8_t> data;

  const uint8_t* y() const {
    return data.data();
  }
  const uint8_t* u() const {
    return y() + static_cast<size_t>(y_stride) * height;
  }
  const uint8_t* v() const {
    return u() + static_cast<size_t>(u_stride) * chroma_height;
  }
  uint8_t* y() {
    return data.data();
  }
  uint8_t* u() {
    return y() + static_cast<size_t>(y_stride) * height;
  }
  uint8_t* v() {
    return u() + static_cast<size_t>(u_stride) * chroma_height;
  }
};

// Converts a framebuffer to BT.601 limited-range I420.
bool rgb_to_i420(const frame_view& source, i420_frame* destination);

}  // namespace gui2_backend

#endif
