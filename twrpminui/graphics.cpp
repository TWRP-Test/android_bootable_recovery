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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <time.h>

#include <cutils/properties.h>
#include "twrpminui/minui.h"
#include "graphics.h"
// For std::min and std::max
#include <algorithm>

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static minui_backend* gr_backend = NULL;

static int overscan_percent = OVERSCAN_PERCENT;
static int overscan_offset_x = 0;
static int overscan_offset_y = 0;

GRSurface* gr_draw = NULL;

static GRPixelFormat gr_target_format = GRPixelFormat::UNKNOWN;
static int gr_physical_width_mm = 0;
static int gr_physical_height_mm = 0;
static GRRect gr_frame_damage = { 0, 0, 0, 0 };
unsigned int gr_rotation = 0;

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

    left = std::clamp(left, 0, gr_draw->width);
    top = std::clamp(top, 0, gr_draw->height);
    right = std::clamp(right, 0, gr_draw->width);
    bottom = std::clamp(bottom, 0, gr_draw->height);
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

GRPixelFormat gr_pixel_format(void)
{
    return gr_target_format;
}

void gr_set_pixel_format(GRPixelFormat format)
{
    if (format != GRPixelFormat::UNKNOWN)
        gr_target_format = format;
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
    const int target_bpp = gr_target_format == GRPixelFormat::RGB565 ? 2 : 4;
    if (gr_draw->pixel_bytes != target_bpp)
        return -1;

    // LVGL's B,G,R,X buffer is already in the byte order of a BGRA target.
    // Keep this common no-rotation path as a row copy. RGBX is intentionally
    // not included: TWRP's RGBX target is R,G,B,X and needs an R/B swap.
    if (gr_target_format == GRPixelFormat::BGRA8888 && gr_rotation == 0 &&
        dx >= 0 && dy >= 0 && dx + width <= logical_width && dy + height <= logical_height &&
        row_bytes == width * 4) {
        for (int sy = 0; sy < height; ++sy) {
            memcpy(gr_draw->data + (dy + sy) * gr_draw->row_bytes + dx * 4,
                   src + sy * row_bytes, width * 4);
        }
        gr_damage(dx, dy, dx + width, dy + height);
        return 0;
    }

    if ((gr_target_format == GRPixelFormat::RGBA8888 ||
         gr_target_format == GRPixelFormat::RGBX8888) &&
        gr_rotation == 0 && dx >= 0 && dy >= 0 &&
        dx + width <= logical_width && dy + height <= logical_height &&
        row_bytes == width * 4) {
        for (int sy = 0; sy < height; ++sy) {
            uint8_t* destination = gr_draw->data +
                    (dy + sy) * gr_draw->row_bytes + dx * 4;
            raw_bgrx_to_rgbx(src + sy * row_bytes, destination, width);
        }
        gr_damage(dx, dy, dx + width, dy + height);
        return 0;
    }

    // The normal phone path is unrotated and 32 bpp. Convert one complete
    // pixel as a uint32_t in that case. On little-endian machines LVGL's
    // source word is [B,G,R,X]. The masks below produce the requested byte
    // order while forcing the unused/alpha byte to opaque.
    if (target_bpp == 4 && gr_rotation == 0 &&
        dx >= 0 && dy >= 0 && dx + width <= logical_width && dy + height <= logical_height &&
        row_bytes == width * 4) {
        for (int sy = 0; sy < height; ++sy) {
            const uint32_t* sp = reinterpret_cast<const uint32_t*>(src + sy * row_bytes);
            uint32_t* dp = reinterpret_cast<uint32_t*>(
                    gr_draw->data + (dy + sy) * gr_draw->row_bytes + dx * 4);
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
        gr_damage(dx, dy, dx + width, dy + height);
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

            const int px = ROTATION_X_DISP(lx, ly, gr_draw->width);
            const int py = ROTATION_Y_DISP(lx, ly, gr_draw->height);
            if (px < 0 || px >= gr_draw->width || py < 0 || py >= gr_draw->height)
                continue;

            const uint8_t* sp = src + sy * row_bytes + sx * 4;
            uint8_t* dp = gr_draw->data + py * gr_draw->row_bytes + px * target_bpp;
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

    const int x0 = ROTATION_X_DISP(dx, dy, gr_draw->width);
    const int y0 = ROTATION_Y_DISP(dx, dy, gr_draw->height);
    const int x1 = ROTATION_X_DISP(dx + width, dy + height, gr_draw->width);
    const int y1 = ROTATION_Y_DISP(dx + width, dy + height, gr_draw->height);
    gr_damage(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 1,
              std::max(y0, y1) + 1);
    return 0;
}

void gr_flip() {
    gr_draw = gr_backend->flip(gr_backend);
    gr_reset_damage();
}

int gr_init(void)
{
    gr_draw = NULL;

#ifdef RECOVERY_FORCE_RGB_565
    gr_target_format = GRPixelFormat::RGB565;
#elif defined(RECOVERY_RGBA)
    gr_target_format = GRPixelFormat::RGBA8888;
#elif defined(RECOVERY_RGBX)
    gr_target_format = GRPixelFormat::RGBX8888;
#elif defined(RECOVERY_BGRA)
    gr_target_format = GRPixelFormat::BGRA8888;
#elif defined(RECOVERY_ABGR)
    gr_target_format = GRPixelFormat::ABGR8888;
#elif defined(RECOVERY_ARGB)
    gr_target_format = GRPixelFormat::ARGB8888;
#else
    gr_target_format = GRPixelFormat::UNKNOWN;
#endif

    char gr_rotation_string[PROPERTY_VALUE_MAX];
    char default_rotation[4];
    snprintf(default_rotation, 4, "%d", TW_ROTATION);
    property_get("persist.twrp.rotation", gr_rotation_string, default_rotation);
    gr_rotation = atoi(gr_rotation_string);
    if (!(gr_rotation == 90 || gr_rotation == 180 || gr_rotation == 270))
        gr_rotation = 0;

#ifdef MSM_BSP
    gr_backend = open_overlay();
    if (gr_backend) {
        gr_draw = gr_backend->init(gr_backend);
        if (!gr_draw) {
            gr_backend->exit(gr_backend);
        } else
            printf("Using overlay graphics.\n");
    }
#endif

#ifdef HAS_DRM
    if (!gr_backend || !gr_draw) {
        gr_backend = open_drm();
        gr_draw = gr_backend->init(gr_backend);
        if (gr_draw)
            printf("Using drm graphics.\n");
    }
#else
    printf("Skipping drm graphics -- not present in build tree\n");
#endif

    if (!gr_backend || !gr_draw) {
        gr_backend = open_fbdev();
        gr_draw = gr_backend->init(gr_backend);
        if (gr_draw == NULL) {
            return -1;
        } else
            printf("Using fbdev graphics.\n");
    }

    overscan_offset_x = gr_draw->width * overscan_percent / 100;
    overscan_offset_y = gr_draw->height * overscan_percent / 100;

    gr_flip();
    gr_flip();

    return 0;
}

void gr_exit(void)
{
    if (gr_backend == NULL)
        return;

    minui_backend* backend = gr_backend;
    gr_backend = NULL;
    backend->exit(backend);
    gr_draw = NULL;
}

int gr_fb_width(void)
{
    return (gr_rotation == 0 || gr_rotation == 180) ?
            gr_draw->width  - 2 * overscan_offset_x :
            gr_draw->height - 2 * overscan_offset_y;
}

int gr_fb_height(void)
{
    return (gr_rotation == 0 || gr_rotation == 180) ?
            gr_draw->height - 2 * overscan_offset_y :
            gr_draw->width  - 2 * overscan_offset_x;
}

bool gr_fb_physical_size(int* width_mm, int* height_mm)
{
    if (!width_mm || !height_mm || gr_physical_width_mm <= 0 || gr_physical_height_mm <= 0)
        return false;
    const bool upright = gr_rotation == 0 || gr_rotation == 180;
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
    *format = gr_pixel_format();

    const size_t frame_size = static_cast<size_t>(gr_draw->height) *
                              static_cast<size_t>(gr_draw->row_bytes);
    if (!destination || capacity < frame_size)
        return -2;

    memcpy(destination, gr_draw->data, frame_size);
    return 0;
}

int gr_fb_pixel_bytes(void)
{
    return gr_draw ? gr_draw->pixel_bytes : 0;
}

void gr_fb_blank(bool blank)
{
    gr_backend->blank(gr_backend, blank);
}
