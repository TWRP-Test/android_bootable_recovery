#include "gui2_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "src/themes/default/lv_theme_default.h"
#include "twrpminui/minui.h"

static bool frame_dirty;
static void* display_buffer;
static uint64_t last_recording_sample_ms;
// Keep a complete frame independent of DRM scanout buffers.
static std::vector<uint8_t> recording_shadow_buffer;
static int recording_shadow_width;
static int recording_shadow_height;

static void submit_recording_frame(gui2_backend::screen_backend* screen, uint64_t monotonic_ms) {
  if (screen == nullptr || !screen->is_recording()) {
    last_recording_sample_ms = 0;
    return;
  }

  // Sample unchanged screens at the configured rate.
  const int fps = std::clamp(screen->recording_fps(), 1, 60);
  const uint64_t interval_ms = std::max<uint64_t>(1, 1000 / fps);
  if (last_recording_sample_ms != 0 && monotonic_ms < last_recording_sample_ms + interval_ms)
    return;
  last_recording_sample_ms = monotonic_ms;

  if (recording_shadow_buffer.empty() || recording_shadow_width <= 0 ||
      recording_shadow_height <= 0)
    return;

  const int width = recording_shadow_width;
  const int height = recording_shadow_height;
  const int row_bytes = width * 4;

  const gui2_backend::frame_view frame{ recording_shadow_buffer.data(), width, height, row_bytes,
                                        gui2_backend::frame_pixel_format::BGRA8888 };
  screen->submit_frame(frame, monotonic_ms);
}

static void flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;

  if (gr_blit_raw(px_map, width, height, width * 4, area->x1, area->y1) < 0) {
    fprintf(stderr, "gui2: unable to submit framebuffer data\n");
  } else {
    // Copy flushed pixels to the capture frame before DRM presentation.
    if (!recording_shadow_buffer.empty()) {
      const int left = std::max(0, static_cast<int>(area->x1));
      const int top = std::max(0, static_cast<int>(area->y1));
      const int right = std::min(recording_shadow_width, static_cast<int>(area->x2) + 1);
      const int bottom = std::min(recording_shadow_height, static_cast<int>(area->y2) + 1);
      if (right > left && bottom > top) {
        for (int y = top; y < bottom; ++y) {
          const int source_y = y - area->y1;
          const int source_x = left - area->x1;
          std::memcpy(recording_shadow_buffer.data() +
                          static_cast<size_t>(y) * recording_shadow_width * 4 +
                          static_cast<size_t>(left) * 4,
                      px_map + static_cast<size_t>(source_y) * width * 4 +
                          static_cast<size_t>(source_x) * 4,
                      static_cast<size_t>(right - left) * 4);
        }
      }
    }
    frame_dirty = true;
  }

  lv_display_flush_ready(display);
}

bool gui2_display_present(gui2_backend::screen_backend* screen, uint64_t monotonic_ms) {
  // A refresh may contain multiple flushes; present before capturing.
  const bool presented = frame_dirty;
  if (presented) {
    gr_flip();
    frame_dirty = false;
  }

  submit_recording_frame(screen, monotonic_ms);
  return presented;
}

void gui2_display_deinit(void) {
  free(display_buffer);
  display_buffer = nullptr;
  frame_dirty = false;
  last_recording_sample_ms = 0;
  recording_shadow_buffer.clear();
  recording_shadow_width = 0;
  recording_shadow_height = 0;
}

lv_display_t* gui2_display_init(void) {
  const int width = gr_fb_width();
  const int height = gr_fb_height();

  if (width <= 0 || height <= 0 || gr_fb_pixel_bytes() <= 0) {
    fprintf(stderr, "gui2: invalid framebuffer (%dx%d, %d Bpp)\n", width, height,
            gr_fb_pixel_bytes());
    return nullptr;
  }

  lv_display_t* display = lv_display_create(width, height);
  if (!display) return nullptr;

  const int dpi = std::clamp(width * 160 / 480, 160, 360);
  lv_display_set_dpi(display, dpi);
  lv_theme_t* theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE),
                                            lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_display_set_theme(display, theme);

  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_flush_cb(display, flush_cb);

  const int buffer_height = std::min(height, 480);
  const size_t buffer_size = static_cast<size_t>(width) * buffer_height * 4;
  void* buffer = calloc(1, buffer_size);
  if (!buffer) {
    lv_display_delete(display);
    return nullptr;
  }

  lv_display_set_buffers(display, buffer, nullptr, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  display_buffer = buffer;
  recording_shadow_width = width;
  recording_shadow_height = height;
  recording_shadow_buffer.assign(static_cast<size_t>(width) * height * 4, 0);
  return display;
}
