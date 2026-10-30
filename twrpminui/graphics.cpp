/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Baseline: current minui/graphics.cpp; local differences are marked TWRP.

#include "graphics.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <new>

#include <android-base/properties.h>

#include "graphics_drm.h"
#include <algorithm>
#include <limits>
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include "graphics_fbdev.h"
#include "twrpminui/minui.h"

// TWRP: BoardConfig fallback defaults; minui properties still take priority.
#ifndef OVERSCAN_PERCENT
#define OVERSCAN_PERCENT 0
#endif
#ifndef TW_ROTATION
#define TW_ROTATION 0
#endif

static GRFont* gr_font = nullptr;
static MinuiBackend* gr_backend = nullptr;

static int overscan_offset_x = 0;
static int overscan_offset_y = 0;

static uint32_t gr_current = ~0;

// gr_draw is owned by backends.
static GRSurface* gr_draw = nullptr;
static GRRotation rotation = GRRotation::NONE;
static PixelFormat pixel_format = PixelFormat::UNKNOWN;
// The graphics backend list that provides fallback options for the default backend selection.
// For example, it will fist try DRM, then try FBDEV if DRM is unavailable.
constexpr auto default_backends = {
    GraphicsBackend::DRM,
    GraphicsBackend::FBDEV };

// TWRP BEGIN: explicit LVGL/capture byte order and physical panel metadata.
static GRPixelFormat gr_target_format = GRPixelFormat::UNKNOWN;
static int gr_physical_width_mm = 0, gr_physical_height_mm = 0;
static GRRect gr_frame_damage = {0, 0, 0, 0};
static GRPixelFormat gr_format_from_property(const std::string& format) {
  if (format == "RGBA_8888") return GRPixelFormat::RGBA8888;
  if (format == "RGBX_8888") return GRPixelFormat::RGBX8888;
  if (format == "BGRA_8888") return GRPixelFormat::BGRA8888;
  if (format == "ABGR_8888") return GRPixelFormat::ABGR8888;
  if (format == "ARGB_8888") return GRPixelFormat::ARGB8888;
  if (format == "RGB_565") return GRPixelFormat::RGB565;
  return GRPixelFormat::UNKNOWN;
}
// TWRP END

static bool outside(int x, int y) {
  auto swapped = (rotation == GRRotation::LEFT || rotation == GRRotation::RIGHT);
  return x < 0 || x >= (swapped ? gr_draw->height : gr_draw->width) || y < 0 ||
         y >= (swapped ? gr_draw->width : gr_draw->height);
}

const GRFont* gr_sys_font() {
  return gr_font;
}

PixelFormat gr_pixel_format() {
  return pixel_format;
}

int gr_measure(const GRFont* font, const char* s) {
  if (font == nullptr) {
    return -1;
  }

  return font->char_width * strlen(s);
}

int gr_font_size(const GRFont* font, int* x, int* y) {
  if (font == nullptr) {
    return -1;
  }

  *x = font->char_width;
  *y = font->char_height;
  return 0;
}

// Blends gr_current onto pix value, assumes alpha as most significant byte.
static inline uint32_t pixel_blend_argb(uint8_t alpha, uint32_t pix) {
  if (alpha == 255) return gr_current;
  if (alpha == 0) return pix;
  uint32_t pix_r = pix & 0xff;
  uint32_t pix_g = pix & 0xff00;
  uint32_t pix_b = pix & 0xff0000;
  uint32_t cur_r = gr_current & 0xff;
  uint32_t cur_g = gr_current & 0xff00;
  uint32_t cur_b = gr_current & 0xff0000;

  uint32_t out_r = (pix_r * (255 - alpha) + cur_r * alpha) / 255;
  uint32_t out_g = (pix_g * (255 - alpha) + cur_g * alpha) / 255;
  uint32_t out_b = (pix_b * (255 - alpha) + cur_b * alpha) / 255;

  return (out_r & 0xff) | (out_g & 0xff00) | (out_b & 0xff0000) | (gr_current & 0xff000000);
}

static inline uint32_t pixel_blend_rgba(uint8_t alpha, uint32_t pix) {
  if (alpha == 255) return gr_current;
  if (alpha == 0) return pix;
  uint32_t pix_r = pix & 0xff00;
  uint32_t pix_g = pix & 0xff0000;
  uint32_t pix_b = pix & 0xff000000;
  uint32_t cur_r = gr_current & 0xff00;
  uint32_t cur_g = gr_current & 0xff0000;
  uint32_t cur_b = gr_current & 0xff000000;

  uint32_t out_r = (pix_r * (255 - alpha) + cur_r * alpha) / 255;
  uint32_t out_g = (pix_g * (255 - alpha) + cur_g * alpha) / 255;
  uint32_t out_b = (pix_b * (255 - alpha) + cur_b * alpha) / 255;

  return (gr_current & 0xff) | (out_r & 0xff00) | (out_g & 0xff0000) | (out_b & 0xff000000);
}

static inline uint32_t pixel_blend(uint8_t alpha, uint32_t pix) {
  if (pixel_format == PixelFormat::RGBA) {
    return pixel_blend_rgba(alpha, pix);
  }
  return pixel_blend_argb(alpha, pix);
}

static inline uint32_t get_alphamask() {
  if (pixel_format == PixelFormat::RGBA) {
    return 0x000000ff;
  }
  return 0xff000000;
}

static inline uint8_t get_alpha_shift() {
  if (pixel_format == PixelFormat::RGBA) {
    return 0;
  }
  return 24;
}

static inline uint8_t get_alpha(uint32_t pix) {
  return static_cast<uint8_t>((pix & (gr_current & get_alphamask())) >> get_alpha_shift());
}

// Increments pixel pointer right, with current rotation.
static void incr_x(uint32_t** p, int row_pixels) {
  if (rotation == GRRotation::LEFT) {
    *p = *p - row_pixels;
  } else if (rotation == GRRotation::RIGHT) {
    *p = *p + row_pixels;
  } else if (rotation == GRRotation::DOWN) {
    *p = *p - 1;
  } else {  // GRRotation::NONE
    *p = *p + 1;
  }
}

// Increments pixel pointer down, with current rotation.
static void incr_y(uint32_t** p, int row_pixels) {
  if (rotation == GRRotation::LEFT) {
    *p = *p + 1;
  } else if (rotation == GRRotation::RIGHT) {
    *p = *p - 1;
  } else if (rotation == GRRotation::DOWN) {
    *p = *p - row_pixels;
  } else {  // GRRotation::NONE
    *p = *p + row_pixels;
  }
}

// Returns pixel pointer at given coordinates with rotation adjustment.
static uint32_t* PixelAt(GRSurface* surface, int x, int y, int row_pixels) {
  switch (rotation) {
    case GRRotation::NONE:
      return reinterpret_cast<uint32_t*>(surface->data()) + y * row_pixels + x;
    case GRRotation::RIGHT:
      return reinterpret_cast<uint32_t*>(surface->data()) + x * row_pixels + (surface->width - 1 - y); // TWRP: keep pixels within the rotated row.
    case GRRotation::DOWN:
      return reinterpret_cast<uint32_t*>(surface->data()) + (surface->height - 1 - y) * row_pixels +
             (surface->width - 1 - x);
    case GRRotation::LEFT:
      return reinterpret_cast<uint32_t*>(surface->data()) + (surface->height - 1 - x) * row_pixels +
             y;
    default:
      printf("invalid rotation %d", static_cast<int>(rotation));
  }
  return nullptr;
}

static void TextBlend(const uint8_t* src_p, int src_row_bytes, uint32_t* dst_p, int dst_row_pixels,
                      int width, int height) {
  uint8_t alpha_current = get_alpha(gr_current);
  for (int j = 0; j < height; ++j) {
    const uint8_t* sx = src_p;
    uint32_t* px = dst_p;
    for (int i = 0; i < width; ++i, incr_x(&px, dst_row_pixels)) {
      uint8_t a = *sx++;
      if (alpha_current < 255) a = (static_cast<uint32_t>(a) * alpha_current) / 255;
      *px = pixel_blend(a, *px);
    }
    src_p += src_row_bytes;
    incr_y(&dst_p, dst_row_pixels);
  }
}

void gr_text(const GRFont* font, int x, int y, const char* s, bool bold) {
  if (!font || !font->texture || (gr_current & get_alphamask()) == 0) return;

  if (font->texture->pixel_bytes != 1) {
    printf("gr_text: font has wrong format\n");
    return;
  }

  bold = bold && (font->texture->height != font->char_height);

  x += overscan_offset_x;
  y += overscan_offset_y;

  unsigned char ch;
  while ((ch = *s++)) {
    if (outside(x, y) || outside(x + font->char_width - 1, y + font->char_height - 1)) break;

    if (ch < ' ' || ch > '~') {
      ch = '?';
    }

    int row_pixels = gr_draw->row_bytes / gr_draw->pixel_bytes;
    const uint8_t* src_p = font->texture->data() + ((ch - ' ') * font->char_width) +
                           (bold ? font->char_height * font->texture->row_bytes : 0);
    uint32_t* dst_p = PixelAt(gr_draw, x, y, row_pixels);

    TextBlend(src_p, font->texture->row_bytes, dst_p, row_pixels, font->char_width,
              font->char_height);

    x += font->char_width;
  }
}

void gr_texticon(int x, int y, const GRSurface* icon) {
  if (icon == nullptr) return;

  if (icon->pixel_bytes != 1) {
    printf("gr_texticon: source has wrong format\n");
    return;
  }

  x += overscan_offset_x;
  y += overscan_offset_y;

  if (outside(x, y) || outside(x + icon->width - 1, y + icon->height - 1)) return;

  int row_pixels = gr_draw->row_bytes / gr_draw->pixel_bytes;
  const uint8_t* src_p = icon->data();
  uint32_t* dst_p = PixelAt(gr_draw, x, y, row_pixels);
  TextBlend(src_p, icon->row_bytes, dst_p, row_pixels, icon->width, icon->height);
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
  uint32_t r32 = r, g32 = g, b32 = b, a32 = a;
  if (pixel_format == PixelFormat::ARGB || pixel_format == PixelFormat::BGRA) {
    gr_current = (a32 << 24) | (r32 << 16) | (g32 << 8) | b32;
  } else if (pixel_format == PixelFormat::RGBA) {
    gr_current = (b32 << 24) | (g32 << 16) | (r32 << 8) | a32;
  } else {
    gr_current = (a32 << 24) | (b32 << 16) | (g32 << 8) | r32;
  }
}

void gr_clear() {
  if ((gr_current & 0xff) == ((gr_current >> 8) & 0xff) &&
      (gr_current & 0xff) == ((gr_current >> 16) & 0xff) &&
      (gr_current & 0xff) == ((gr_current >> 24) & 0xff) &&
      gr_draw->row_bytes == gr_draw->width * gr_draw->pixel_bytes) {
    memset(gr_draw->data(), gr_current & 0xff, gr_draw->height * gr_draw->row_bytes);
  } else {
    uint32_t* px = reinterpret_cast<uint32_t*>(gr_draw->data());
    int row_diff = gr_draw->row_bytes / gr_draw->pixel_bytes - gr_draw->width;
    for (int y = 0; y < gr_draw->height; ++y) {
      for (int x = 0; x < gr_draw->width; ++x) {
        *px++ = gr_current;
      }
      px += row_diff;
    }
  }
}

void gr_fill(int x1, int y1, int x2, int y2) {
  x1 += overscan_offset_x;
  y1 += overscan_offset_y;

  x2 += overscan_offset_x;
  y2 += overscan_offset_y;

  if (outside(x1, y1) || outside(x2 - 1, y2 - 1)) return;

  int row_pixels = gr_draw->row_bytes / gr_draw->pixel_bytes;
  uint32_t* p = PixelAt(gr_draw, x1, y1, row_pixels);
  uint8_t alpha = get_alpha(gr_current);
  if (alpha > 0) {
    for (int y = y1; y < y2; ++y) {
      uint32_t* px = p;
      for (int x = x1; x < x2; ++x) {
        *px = pixel_blend(alpha, *px);
        incr_x(&px, row_pixels);
      }
      incr_y(&p, row_pixels);
    }
  }
}

void gr_blit(const GRSurface* source, int sx, int sy, int w, int h, int dx, int dy) {
  if (source == nullptr) return;

  if (gr_draw->pixel_bytes != source->pixel_bytes) {
    printf("gr_blit: source has wrong format\n");
    return;
  }

  dx += overscan_offset_x;
  dy += overscan_offset_y;

  if (outside(dx, dy) || outside(dx + w - 1, dy + h - 1)) return;

  if (rotation != GRRotation::NONE) {
    int src_row_pixels = source->row_bytes / source->pixel_bytes;
    int row_pixels = gr_draw->row_bytes / gr_draw->pixel_bytes;
    const uint32_t* src_py =
        reinterpret_cast<const uint32_t*>(source->data()) + sy * source->row_bytes / 4 + sx;
    uint32_t* dst_py = PixelAt(gr_draw, dx, dy, row_pixels);

    for (int y = 0; y < h; y += 1) {
      const uint32_t* src_px = src_py;
      uint32_t* dst_px = dst_py;
      for (int x = 0; x < w; x += 1) {
        *dst_px = *src_px++;
        incr_x(&dst_px, row_pixels);
      }
      src_py += src_row_pixels;
      incr_y(&dst_py, row_pixels);
    }
  } else {
    const uint8_t* src_p = source->data() + sy * source->row_bytes + sx * source->pixel_bytes;
    uint8_t* dst_p = gr_draw->data() + dy * gr_draw->row_bytes + dx * gr_draw->pixel_bytes;

    for (int i = 0; i < h; ++i) {
      memcpy(dst_p, src_p, w * source->pixel_bytes);
      src_p += source->row_bytes;
      dst_p += gr_draw->row_bytes;
    }
  }
}

unsigned int gr_get_width(const GRSurface* surface) {
  if (surface == nullptr) {
    return 0;
  }
  return surface->width;
}

unsigned int gr_get_height(const GRSurface* surface) {
  if (surface == nullptr) {
    return 0;
  }
  return surface->height;
}

int gr_init_font(const char* name, GRFont** dest) {
  // TWRP: pair allocation with gr_exit ownership across GUI2 lifecycles.
  GRFont* font = new (std::nothrow) GRFont{};
  if (font == nullptr) {
    return -1;
  }

  int res = res_create_alpha_surface(name, &(font->texture));
  if (res < 0) {
    delete font;
    return res;
  }

  // The font image should be a 96x2 array of character images.  The
  // columns are the printable ASCII characters 0x20 - 0x7f.  The
  // top row is regular text; the bottom row is bold.
  font->char_width = font->texture->width / 96;
  font->char_height = font->texture->height / 2;

  *dest = font;

  return 0;
}

void gr_flip() {
  gr_draw = gr_backend->Flip();
  // TWRP: consumed by DRM partial presentation.
  gr_reset_damage();
}

std::unique_ptr<MinuiBackend> create_backend(GraphicsBackend backend) {
  switch (backend) {
    case GraphicsBackend::DRM:
      return std::make_unique<MinuiBackendDrm>();
    case GraphicsBackend::FBDEV:
      return std::make_unique<MinuiBackendFbdev>();
    default:
      return nullptr;
  }
}

int gr_init() {
  return gr_init(default_backends);
}

int gr_init(std::initializer_list<GraphicsBackend> backends) {
  // TWRP: initialization failure and repeated GUI2 lifecycles share cleanup.
  gr_exit();
  gr_reset_damage();
  // pixel_format needs to be set before loading any resources or initializing backends.
  std::string format = android::base::GetProperty("ro.minui.pixel_format", "");
  if (format == "ABGR_8888") {
    pixel_format = PixelFormat::ABGR;
  } else if (format == "RGBX_8888") {
    pixel_format = PixelFormat::RGBX;
  } else if (format == "ARGB_8888") {
    pixel_format = PixelFormat::ARGB;
  } else if (format == "BGRA_8888") {
    pixel_format = PixelFormat::BGRA;
  } else if (format == "RGBA_8888") {
    pixel_format = PixelFormat::RGBA;
  } else {
    pixel_format = PixelFormat::UNKNOWN;
  }

  // TWRP: BoardConfig is a fallback; explicit minui properties take priority.
  if (format.empty()) {
#if defined(RECOVERY_FORCE_RGB_565)
    gr_set_pixel_format(GRPixelFormat::RGB565);
#elif defined(RECOVERY_RGBA)
    gr_set_pixel_format(GRPixelFormat::RGBA8888);
#elif defined(RECOVERY_RGBX)
    gr_set_pixel_format(GRPixelFormat::RGBX8888);
#elif defined(RECOVERY_BGRA)
    gr_set_pixel_format(GRPixelFormat::BGRA8888);
#elif defined(RECOVERY_ABGR)
    gr_set_pixel_format(GRPixelFormat::ABGR8888);
#elif defined(RECOVERY_ARGB)
    gr_set_pixel_format(GRPixelFormat::ARGB8888);
#else
    gr_set_pixel_format(GRPixelFormat::RGB565);
#endif
  } else {
    gr_set_pixel_format(gr_format_from_property(format));
  }

  int ret = gr_init_font("font", &gr_font);
  if (ret != 0) {
    printf("Failed to init font: %d, continuing graphic backend initialization without font file\n",
           ret);
  }

  std::unique_ptr<MinuiBackend> minui_backend;
  for (GraphicsBackend backend : backends) {
    minui_backend = create_backend(backend);
    if (!minui_backend) {
      printf("gr_init: minui_backend %d is a nullptr\n", static_cast<int>(backend));
      continue;
    }
    gr_draw = minui_backend->Init();
    if (gr_draw) break;
  }

  if (!gr_draw) {
    return -1;
  }

  gr_backend = minui_backend.release();

  int overscan_percent = android::base::GetIntProperty("ro.minui.overscan_percent", OVERSCAN_PERCENT);
  overscan_offset_x = gr_draw->width * overscan_percent / 100;
  overscan_offset_y = gr_draw->height * overscan_percent / 100;

  gr_flip();
  gr_flip();
  if (!gr_draw) {
    printf("gr_init: gr_draw becomes nullptr after gr_flip\n");
    gr_exit();
    return -1;
  }

  std::string rotation_str =
      android::base::GetProperty("ro.minui.default_rotation", "ROTATION_NONE");
  if (rotation_str == "ROTATION_RIGHT") {
    gr_rotate(GRRotation::RIGHT);
  } else if (rotation_str == "ROTATION_DOWN") {
    gr_rotate(GRRotation::DOWN);
  } else if (rotation_str == "ROTATION_LEFT") {
    gr_rotate(GRRotation::LEFT);
  } else {  // "ROTATION_NONE" or unknown string
    gr_rotate(GRRotation::NONE);
  }

  // TWRP: persisted panel rotation overrides the minui default (d32b7ebf).
  const int panel_rotation = android::base::GetIntProperty("persist.twrp.rotation", TW_ROTATION);
  if (android::base::GetProperty("ro.minui.default_rotation", "").empty() ||
      !android::base::GetProperty("persist.twrp.rotation", "").empty()) {
    switch (panel_rotation) {
      case 90: gr_rotate(GRRotation::RIGHT); break;
      case 180: gr_rotate(GRRotation::DOWN); break;
      case 270: gr_rotate(GRRotation::LEFT); break;
      default: gr_rotate(GRRotation::NONE); break;
    }
  }

  if (gr_draw->pixel_bytes != 4) {
    printf("gr_init: Only 4-byte pixel formats supported\n");
  }

  return 0;
}

void gr_exit() {
  delete gr_backend;
  gr_backend = nullptr;
  gr_draw = nullptr;
  gr_physical_width_mm = gr_physical_height_mm = 0; // TWRP: no stale panel metrics.

  // TWRP: the native font owns its texture as well as the font descriptor.
  if (gr_font) delete gr_font->texture;
  delete gr_font;
  gr_font = nullptr;
}

int gr_fb_width() {
  return (rotation == GRRotation::LEFT || rotation == GRRotation::RIGHT)
             ? gr_draw->height - 2 * overscan_offset_y
             : gr_draw->width - 2 * overscan_offset_x;
}

int gr_fb_height() {
  return (rotation == GRRotation::LEFT || rotation == GRRotation::RIGHT)
             ? gr_draw->width - 2 * overscan_offset_x
             : gr_draw->height - 2 * overscan_offset_y;
}

void gr_fb_blank(bool blank) {
  gr_backend->Blank(blank);
}

void gr_fb_blank(bool blank, int index) {
  gr_backend->Blank(blank, static_cast<MinuiBackend::DrmConnector>(index));
}

void gr_rotate(GRRotation rot) {
  rotation = rot;
}

GRRotation gr_get_rotation() {
  return rotation;
}

bool gr_has_multiple_connectors() {
  return gr_backend->HasMultipleConnectors();
}

// TWRP BEGIN: LVGL flush, damage, screenshot and recorder extensions (b7457030, 02827311).
static void raw_bgrx_to_rgbx(const uint8_t* source, uint8_t* destination,
                             size_t pixel_count)
{
    size_t i = 0;

#if defined(__aarch64__) || defined(__ARM_NEON)
    for (; i + 16 <= pixel_count; i += 16) {
        // vld4q_u8 de-interleaves 16 source pixels from B,G,R,X into four
        // vectors. Re-interleave them as R,G,B,X for the TWRP RGBX target.
        const uint8x16x4_t source_pixels = vld4q_u8(source + i * 4);
        uint8x16x4_t destination_pixels;
        destination_pixels.val[0] = source_pixels.val[2];
        destination_pixels.val[1] = source_pixels.val[1];
        destination_pixels.val[2] = source_pixels.val[0];
        destination_pixels.val[3] = vdupq_n_u8(0xff);
        vst4q_u8(destination + i * 4, destination_pixels);
    }
#endif

    // Handle the tail, and provide the implementation used on non-NEON
    // targets. The source word is [B,G,R,X] on Android's little-endian CPUs.
    for (; i < pixel_count; ++i) {
        uint32_t pixel;
        memcpy(&pixel, source + i * 4, sizeof(pixel));
        pixel = ((pixel & 0x0000ff00U) |
                 ((pixel & 0x00ff0000U) >> 16) |
                 ((pixel & 0x000000ffU) << 16) |
                 0xff000000U);
        memcpy(destination + i * 4, &pixel, sizeof(pixel));
    }
}

void gr_damage(int left, int top, int right, int bottom)
{
    if (!gr_draw)
        return;

    left = std::clamp(left, 0, static_cast<int>(gr_draw->width));
    top = std::clamp(top, 0, static_cast<int>(gr_draw->height));
    right = std::clamp(right, 0, static_cast<int>(gr_draw->width));
    bottom = std::clamp(bottom, 0, static_cast<int>(gr_draw->height));
    if (left >= right || top >= bottom)
        return;

    if (gr_frame_damage.left >= gr_frame_damage.right ||
        gr_frame_damage.top >= gr_frame_damage.bottom) {
        gr_frame_damage = { left, top, right, bottom };
        return;
    }

    gr_frame_damage.left = std::min(gr_frame_damage.left, left);
    gr_frame_damage.top = std::min(gr_frame_damage.top, top);
    gr_frame_damage.right = std::max(gr_frame_damage.right, right);
    gr_frame_damage.bottom = std::max(gr_frame_damage.bottom, bottom);
}

GRRect gr_get_damage()
{
    return gr_frame_damage;
}

void gr_reset_damage()
{
    gr_frame_damage = { 0, 0, 0, 0 };
}

GRPixelFormat gr_frame_pixel_format()
{
    return gr_target_format;
}

void gr_set_pixel_format(GRPixelFormat format) {
  gr_target_format = format;
  switch (format) {
    case GRPixelFormat::RGB565: pixel_format = PixelFormat::RGB565; break;
    case GRPixelFormat::RGBA8888: pixel_format = PixelFormat::RGBA; break;
    case GRPixelFormat::RGBX8888: pixel_format = PixelFormat::RGBX; break;
    case GRPixelFormat::BGRA8888: pixel_format = PixelFormat::BGRA; break;
    case GRPixelFormat::ABGR8888: pixel_format = PixelFormat::ABGR; break;
    case GRPixelFormat::ARGB8888:
    case GRPixelFormat::XRGB8888: pixel_format = PixelFormat::ARGB; break;
    default: pixel_format = PixelFormat::UNKNOWN; break;
  }
}

void gr_set_physical_size(int width_mm, int height_mm)
{
    gr_physical_width_mm = width_mm;
    gr_physical_height_mm = height_mm;
}

int gr_blit_raw(const void* data, int width, int height, int row_bytes, int dx, int dy)
{
    if (!gr_draw || !data || width <= 0 || height <= 0 || row_bytes < width * 4)
        return -1;

    if (gr_target_format == GRPixelFormat::UNKNOWN)
        return -1;

    // LVGL's XRGB8888 is 0xXXRRGGBB, therefore its little-endian bytes are
    // B,G,R,X. Convert directly into minui's current draw buffer. This avoids
    // an intermediate texture conversion for the completed LVGL raster buffer.
    const uint8_t* src = static_cast<const uint8_t*>(data);
    const int logical_width = gr_fb_width();
    const int logical_height = gr_fb_height();
    // TWRP: honor minui's overscan in the logical, potentially rotated viewport.
    const bool sideways = gr_get_rotation() == GRRotation::RIGHT ||
                          gr_get_rotation() == GRRotation::LEFT;
    const int inset_x = sideways ? overscan_offset_y : overscan_offset_x;
    const int inset_y = sideways ? overscan_offset_x : overscan_offset_y;
    const int target_bpp = gr_target_format == GRPixelFormat::RGB565 ? 2 : 4;
    if (gr_draw->pixel_bytes != target_bpp)
        return -1;

    // LVGL's B,G,R,X buffer is already in the byte order of a BGRA target.
    // Keep this common no-rotation path as a row copy. RGBX is intentionally
    // not included: TWRP's RGBX target is R,G,B,X and needs an R/B swap.
    if (gr_target_format == GRPixelFormat::BGRA8888 && (static_cast<int>(gr_get_rotation()) * 90) == 0 &&
        dx >= 0 && dy >= 0 && dx + width <= logical_width && dy + height <= logical_height &&
        row_bytes == width * 4) {
        for (int sy = 0; sy < height; ++sy) {
            memcpy(gr_draw->data() + (dy + overscan_offset_y + sy) * gr_draw->row_bytes +
                   (dx + overscan_offset_x) * 4,
                   src + sy * row_bytes, width * 4);
        }
        gr_damage(dx + overscan_offset_x, dy + overscan_offset_y,
                  dx + overscan_offset_x + width, dy + overscan_offset_y + height);
        return 0;
    }

    if ((gr_target_format == GRPixelFormat::RGBA8888 ||
         gr_target_format == GRPixelFormat::RGBX8888) &&
        (static_cast<int>(gr_get_rotation()) * 90) == 0 && dx >= 0 && dy >= 0 &&
        dx + width <= logical_width && dy + height <= logical_height &&
        row_bytes == width * 4) {
        for (int sy = 0; sy < height; ++sy) {
            uint8_t* destination = gr_draw->data() +
                    (dy + overscan_offset_y + sy) * gr_draw->row_bytes +
                   (dx + overscan_offset_x) * 4;
            raw_bgrx_to_rgbx(src + sy * row_bytes, destination, width);
        }
        gr_damage(dx + overscan_offset_x, dy + overscan_offset_y,
                  dx + overscan_offset_x + width, dy + overscan_offset_y + height);
        return 0;
    }

    // The normal phone path is unrotated and 32 bpp. Convert one complete
    // pixel as a uint32_t in that case. On little-endian machines LVGL's
    // source word is [B,G,R,X]. The masks below produce the requested byte
    // order while forcing the unused/alpha byte to opaque.
    if (target_bpp == 4 && (static_cast<int>(gr_get_rotation()) * 90) == 0 &&
        dx >= 0 && dy >= 0 && dx + width <= logical_width && dy + height <= logical_height &&
        row_bytes == width * 4) {
        for (int sy = 0; sy < height; ++sy) {
            const uint32_t* sp = reinterpret_cast<const uint32_t*>(src + sy * row_bytes);
            uint32_t* dp = reinterpret_cast<uint32_t*>(
                    gr_draw->data() + (dy + overscan_offset_y + sy) * gr_draw->row_bytes +
                   (dx + overscan_offset_x) * 4);
            for (int sx = 0; sx < width; ++sx) {
                const uint32_t pixel = sp[sx];
                switch (gr_target_format) {
                    case GRPixelFormat::RGBA8888:
                    case GRPixelFormat::RGBX8888:
                        dp[sx] = ((pixel & 0x0000ff00U) |
                                  ((pixel & 0x00ff0000U) >> 16) |
                                  ((pixel & 0x000000ffU) << 16) |
                                  0xff000000U);
                        break;
                    case GRPixelFormat::ABGR8888:
                        dp[sx] = (pixel << 8) | 0xffU;
                        break;
                    case GRPixelFormat::ARGB8888:
                    case GRPixelFormat::XRGB8888:
                        dp[sx] = (__builtin_bswap32(pixel) & 0xffffff00U) | 0xffU;
                        break;
                    default:
                        return -1;
                }
            }
        }
        gr_damage(dx + overscan_offset_x, dy + overscan_offset_y,
                  dx + overscan_offset_x + width, dy + overscan_offset_y + height);
        return 0;
    }

    // LVGL stores each source pixel as the four bytes B,G,R,X. Keep the
    // conversion arithmetic outside the inner format switch. Apart from
    // being cheaper, this makes the byte order of every supported mode
    // explicit (the names here describe bytes in memory).
    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const int lx = dx + sx;
            const int ly = dy + sy;
            if (lx < 0 || lx >= logical_width || ly < 0 || ly >= logical_height)
                continue;

            const int px = ROTATION_X_DISP(lx + inset_x, ly + inset_y, gr_draw->width);
            const int py = ROTATION_Y_DISP(lx + inset_x, ly + inset_y, gr_draw->height);
            if (px < 0 || px >= gr_draw->width || py < 0 || py >= gr_draw->height)
                continue;

            const uint8_t* sp = src + sy * row_bytes + sx * 4;
            uint8_t* dp = gr_draw->data() + py * gr_draw->row_bytes + px * target_bpp;
            const uint8_t b = sp[0];
            const uint8_t g = sp[1];
            const uint8_t r = sp[2];
            const uint8_t a = 0xff;

            if (gr_target_format == GRPixelFormat::RGB565) {
                const uint16_t pixel = static_cast<uint16_t>(((r & 0xf8) << 8) |
                                                               ((g & 0xfc) << 3) |
                                                               (b >> 3));
                memcpy(dp, &pixel, sizeof(pixel));
            }
            else if (gr_target_format == GRPixelFormat::RGBA8888 ||
                     gr_target_format == GRPixelFormat::RGBX8888) {
                dp[0] = r; dp[1] = g; dp[2] = b; dp[3] = a;
            }
            else if (gr_target_format == GRPixelFormat::BGRA8888) {
                dp[0] = b; dp[1] = g; dp[2] = r; dp[3] = a;
            }
            else if (gr_target_format == GRPixelFormat::ABGR8888) {
                dp[0] = a; dp[1] = b; dp[2] = g; dp[3] = r;
            }
            else if (gr_target_format == GRPixelFormat::ARGB8888) {
                dp[0] = a; dp[1] = r; dp[2] = g; dp[3] = b;
            }
            else if (gr_target_format == GRPixelFormat::XRGB8888) {
                dp[0] = a; dp[1] = r; dp[2] = g; dp[3] = b;
            }
            else {
                return -1;
            }
        }
    }

    const int x0 = ROTATION_X_DISP(dx + inset_x, dy + inset_y, gr_draw->width);
    const int y0 = ROTATION_Y_DISP(dx + inset_x, dy + inset_y, gr_draw->height);
    const int x1 = ROTATION_X_DISP(dx + inset_x + width, dy + inset_y + height, gr_draw->width);
    const int y1 = ROTATION_Y_DISP(dx + inset_x + width, dy + inset_y + height, gr_draw->height);
    gr_damage(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 1,
              std::max(y0, y1) + 1);
    return 0;
}

bool gr_fb_physical_size(int* width_mm, int* height_mm)
{
    if (!width_mm || !height_mm || gr_physical_width_mm <= 0 || gr_physical_height_mm <= 0)
        return false;
    const bool upright = (static_cast<int>(gr_get_rotation()) * 90) == 0 || (static_cast<int>(gr_get_rotation()) * 90) == 180;
    *width_mm = upright ? gr_physical_width_mm : gr_physical_height_mm;
    *height_mm = upright ? gr_physical_height_mm : gr_physical_width_mm;
    return true;
}

int gr_copy_frame(void* destination, size_t capacity, int* width, int* height,
                  int* row_bytes, GRPixelFormat* format)
{
    if (!gr_draw || !width || !height || !row_bytes || !format)
        return -1;

    *width = gr_draw->width;
    *height = gr_draw->height;
    *row_bytes = gr_draw->row_bytes;
    *format = gr_frame_pixel_format();

    const size_t frame_size = static_cast<size_t>(gr_draw->height) *
                              static_cast<size_t>(gr_draw->row_bytes);
    if (!destination || capacity < frame_size)
        return -2;

    memcpy(destination, gr_draw->data(), frame_size);
    return 0;
}

int gr_fb_pixel_bytes(void)
{
    return gr_draw ? gr_draw->pixel_bytes : 0;
}

// TWRP END
