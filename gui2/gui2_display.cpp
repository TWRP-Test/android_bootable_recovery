#include "gui2_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

#include "src/themes/default/lv_theme_default.h"
#include "twrpminui/minui.h"

static bool frame_dirty;
static void* display_buffer;

static void flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;

  if (gr_blit_raw(px_map, width, height, width * 4, area->x1, area->y1) < 0) {
    fprintf(stderr, "gui2: unable to submit framebuffer data\n");
  } else {
    frame_dirty = true;
  }

  lv_display_flush_ready(display);
}

bool gui2_display_present(void) {
  if (!frame_dirty) return false;

  // A single LVGL refresh can be split into several flushes.  Present only
  // after all of them have been copied to the minui back buffer.
  gr_flip();
  frame_dirty = false;
  return true;
}

void gui2_display_deinit(void) {
  free(display_buffer);
  display_buffer = nullptr;
  frame_dirty = false;
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

  // LVGL is pixel based, so provide a target-independent logical density.
  // 480 px is treated as the 160-DPI baseline; emux64 (1080 px) becomes
  // 360 DPI. This affects theme paddings and default widget dimensions.
  const int dpi = std::clamp(width * 160 / 480, 160, 360);
  lv_display_set_dpi(display, dpi);
  lv_theme_t* theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE),
                                            lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_display_set_theme(display, theme);

  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_flush_cb(display, flush_cb);

  // A partial buffer avoids rerasterizing and copying the whole 1080x1920
  // screen when a small widget changes.  gr_blit_raw() copies each flushed
  // area synchronously, so one buffer is sufficient here.
  // A taller partial buffer reduces per-flush setup/clip overhead while
  // keeping memory bounded (about 2 MiB at 1080 px wide).
  const int buffer_height = std::min(height, 480);
  const size_t buffer_size = static_cast<size_t>(width) * buffer_height * 4;
  void* buffer = calloc(1, buffer_size);
  if (!buffer) {
    lv_display_delete(display);
    return nullptr;
  }

  lv_display_set_buffers(display, buffer, nullptr, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  display_buffer = buffer;
  return display;
}
