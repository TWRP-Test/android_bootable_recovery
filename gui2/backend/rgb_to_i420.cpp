#include "rgb_to_i420.h"

#include <algorithm>
#include <cstddef>

namespace gui2_backend {
namespace {

int source_bytes_per_pixel(frame_pixel_format format) {
  return format == frame_pixel_format::RGB565 ? 2 : 4;
}

void pixel_to_rgb(const uint8_t* source, frame_pixel_format format, int* red, int* green,
                  int* blue) {
  switch (format) {
    case frame_pixel_format::RGB565: {
      const uint16_t pixel = static_cast<uint16_t>(source[0] | (source[1] << 8));
      *red = ((pixel >> 11) & 0x1f) * 255 / 31;
      *green = ((pixel >> 5) & 0x3f) * 255 / 63;
      *blue = (pixel & 0x1f) * 255 / 31;
      return;
    }
    case frame_pixel_format::RGBX8888:
    case frame_pixel_format::RGBA8888:
      *red = source[0];
      *green = source[1];
      *blue = source[2];
      return;
    case frame_pixel_format::BGRA8888:
      *red = source[2];
      *green = source[1];
      *blue = source[0];
      return;
    case frame_pixel_format::ABGR8888:
      *red = source[3];
      *green = source[2];
      *blue = source[1];
      return;
    case frame_pixel_format::ARGB8888:
    case frame_pixel_format::XRGB8888:
      *red = source[1];
      *green = source[2];
      *blue = source[3];
      return;
  }
}

uint8_t clamp_byte(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

uint8_t rgb_to_y(int red, int green, int blue) {
  return clamp_byte(((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16);
}

uint8_t rgb_to_u(int red, int green, int blue) {
  return clamp_byte(((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128);
}

uint8_t rgb_to_v(int red, int green, int blue) {
  return clamp_byte(((112 * red - 94 * green - 18 * blue + 128) >> 8) + 128);
}

}  // namespace

bool rgb_to_i420(const frame_view& source, i420_frame* destination) {
  if (destination == nullptr || source.data == nullptr || source.width <= 0 || source.height <= 0 ||
      source.row_bytes <= 0 ||
      source.row_bytes < source.width * source_bytes_per_pixel(source.format)) {
    return false;
  }

  const int chroma_width = (source.width + 1) / 2;
  const int chroma_height = (source.height + 1) / 2;
  const size_t y_size = static_cast<size_t>(source.width) * source.height;
  const size_t chroma_size = static_cast<size_t>(chroma_width) * chroma_height;

  destination->width = source.width;
  destination->height = source.height;
  destination->chroma_width = chroma_width;
  destination->chroma_height = chroma_height;
  destination->y_stride = source.width;
  destination->u_stride = chroma_width;
  destination->v_stride = chroma_width;
  destination->data.assign(y_size + chroma_size * 2, 0);

  const int source_bpp = source_bytes_per_pixel(source.format);
  for (int y = 0; y < source.height; ++y) {
    const uint8_t* source_row = source.data + static_cast<size_t>(y) * source.row_bytes;
    uint8_t* y_row = destination->y() + static_cast<size_t>(y) * destination->y_stride;
    for (int x = 0; x < source.width; ++x) {
      int red;
      int green;
      int blue;
      pixel_to_rgb(source_row + static_cast<size_t>(x) * source_bpp, source.format, &red, &green,
                   &blue);
      y_row[x] = rgb_to_y(red, green, blue);
    }
  }

  // Average each 2x2 block; repeat edge pixels for odd dimensions.
  for (int cy = 0; cy < chroma_height; ++cy) {
    for (int cx = 0; cx < chroma_width; ++cx) {
      int red = 0;
      int green = 0;
      int blue = 0;
      int samples = 0;
      for (int dy = 0; dy < 2; ++dy) {
        const int y = std::min(source.height - 1, cy * 2 + dy);
        const uint8_t* source_row = source.data + static_cast<size_t>(y) * source.row_bytes;
        for (int dx = 0; dx < 2; ++dx) {
          const int x = std::min(source.width - 1, cx * 2 + dx);
          int pixel_red;
          int pixel_green;
          int pixel_blue;
          pixel_to_rgb(source_row + static_cast<size_t>(x) * source_bpp, source.format, &pixel_red,
                       &pixel_green, &pixel_blue);
          red += pixel_red;
          green += pixel_green;
          blue += pixel_blue;
          ++samples;
        }
      }
      red /= samples;
      green /= samples;
      blue /= samples;
      destination->u()[static_cast<size_t>(cy) * destination->u_stride + cx] =
          rgb_to_u(red, green, blue);
      destination->v()[static_cast<size_t>(cy) * destination->v_stride + cx] =
          rgb_to_v(red, green, blue);
    }
  }
  return true;
}

}  // namespace gui2_backend
