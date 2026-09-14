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
#include <pixelflinger/pixelflinger.h>
#include "gui/placement.h"
#include "twrpminui/minui.h"
#include "graphics.h"
// For std::min and std::max
#include <algorithm>
#include "twrpminui/truetype.hpp"

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

struct GRFont {
    GRSurface* texture;
    int cwidth;
    int cheight;
};

static minui_backend* gr_backend = NULL;

static int overscan_percent = OVERSCAN_PERCENT;
static int overscan_offset_x = 0;
static int overscan_offset_y = 0;

static unsigned char gr_current_r = 255;
static unsigned char gr_current_g = 255;
static unsigned char gr_current_b = 255;

GRSurface* gr_draw = NULL;

static GGLContext *gr_context = 0;
GGLSurface gr_mem_surface;
static GRPixelFormat gr_target_format = GRPixelFormat::UNKNOWN;
static bool gr_raw_native_frame = false;
static int gr_is_curr_clr_opaque = 0;
static GRRect gr_frame_damage = { 0, 0, 0, 0 };
static GRRect gr_damage_clip = { 0, 0, 0, 0 };
static GRRect gr_object_clip = { 0, 0, 0, 0 };
static bool gr_damage_clip_enabled = false;
static bool gr_object_clip_enabled = false;

unsigned int gr_rotation = 0;

static bool rect_empty(const GRRect& rect)
{
    return rect.left >= rect.right || rect.top >= rect.bottom;
}

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

static GRRect intersect_rects(const GRRect& first, const GRRect& second)
{
    return {
        std::max(first.left, second.left),
        std::max(first.top, second.top),
        std::min(first.right, second.right),
        std::min(first.bottom, second.bottom),
    };
}

static GRRect display_rect(int x, int y, int w, int h)
{
    const int x0 = ROTATION_X_DISP(x, y, gr_draw->width);
    const int y0 = ROTATION_Y_DISP(x, y, gr_draw->height);
    const int x1 = ROTATION_X_DISP(x + w, y + h, gr_draw->width);
    const int y1 = ROTATION_Y_DISP(x + w, y + h, gr_draw->height);
    return {
        std::min(x0, x1), std::min(y0, y1),
        std::max(x0, x1), std::max(y0, y1),
    };
}

static void apply_render_clip()
{
    if (!gr_context)
        return;

    GRRect clip = { 0, 0, gr_draw->width, gr_draw->height };
    bool enabled = false;
    if (gr_damage_clip_enabled) {
        clip = gr_damage_clip;
        enabled = true;
    }
    if (gr_object_clip_enabled) {
        clip = enabled ? intersect_rects(clip, gr_object_clip) : gr_object_clip;
        enabled = true;
    }

    if (!enabled) {
        gr_context->disable(gr_context, GGL_SCISSOR_TEST);
        return;
    }

    clip.left = std::clamp(clip.left, 0, gr_draw->width);
    clip.top = std::clamp(clip.top, 0, gr_draw->height);
    clip.right = std::clamp(clip.right, clip.left, gr_draw->width);
    clip.bottom = std::clamp(clip.bottom, clip.top, gr_draw->height);
    gr_context->scissor(gr_context, clip.left, clip.top,
                        clip.right - clip.left, clip.bottom - clip.top);
    gr_context->enable(gr_context, GGL_SCISSOR_TEST);
}

void gr_damage(int left, int top, int right, int bottom)
{
    if (!gr_draw)
        return;

    left = std::clamp(left, 0, gr_draw->width);
    top = std::clamp(top, 0, gr_draw->height);
    right = std::clamp(right, 0, gr_draw->width);
    bottom = std::clamp(bottom, 0, gr_draw->height);
    GRRect damage = { left, top, right, bottom };
    if (gr_damage_clip_enabled)
        damage = intersect_rects(damage, gr_damage_clip);
    if (gr_object_clip_enabled)
        damage = intersect_rects(damage, gr_object_clip);
    left = damage.left;
    top = damage.top;
    right = damage.right;
    bottom = damage.bottom;
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

void gr_invalidate(int x, int y, int w, int h)
{
    if (!gr_draw || w <= 0 || h <= 0)
        return;

    const GRRect damage = display_rect(x, y, w, h);
    gr_damage(damage.left, damage.top, damage.right, damage.bottom);
}

bool gr_begin_damage_clip()
{
    if (rect_empty(gr_frame_damage))
        return false;

    gr_damage_clip = gr_frame_damage;
    gr_damage_clip_enabled = true;
    apply_render_clip();
    return true;
}

void gr_end_damage_clip()
{
    gr_damage_clip_enabled = false;
    gr_object_clip_enabled = false;
    apply_render_clip();
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

bool gr_raw_frame_native()
{
    return gr_raw_native_frame;
}

void gr_raw_frame_done()
{
    gr_raw_native_frame = false;
}

int gr_textEx_scaleW(int x, int y, const char *s, void* pFont, int max_width, int placement, int scale)
{
    GGLContext *gl = gr_context;
    void* vfont = pFont;
    GRFont *font = (GRFont*) pFont;
    int y_scale = 0, measured_width, measured_height, new_height;

    if (!s || strlen(s) == 0 || !font)
        return 0;

    measured_height = twrpTruetype::gr_ttf_getMaxFontHeight(font);

    if (scale) {
        measured_width = twrpTruetype::gr_ttf_measureEx(s, vfont);
        if (measured_width > max_width) {
            // Adjust font size down until the text fits
            void *new_font = twrpTruetype::gr_ttf_scaleFont(vfont, max_width, measured_width);
            if (!new_font) {
                printf("gr_textEx_scaleW new_font is NULL\n");
                return 0;
            }
            measured_width = twrpTruetype::gr_ttf_measureEx(s, new_font);
            // These next 2 lines adjust the y point based on the new font's height
            new_height = twrpTruetype::gr_ttf_getMaxFontHeight(new_font);
            y_scale = (measured_height - new_height) / 2;
            vfont = new_font;
        }
    } else
        measured_width = twrpTruetype::gr_ttf_measureEx(s, vfont);

    int x_adj = measured_width;
    if (measured_width > max_width)
        x_adj = max_width;

    if (placement != TOP_LEFT && placement != BOTTOM_LEFT && placement != TEXT_ONLY_RIGHT) {
        if (placement == CENTER || placement == CENTER_X_ONLY)
            x -= (x_adj / 2);
        else
            x -= x_adj;
    }

    if (placement != TOP_LEFT && placement != TOP_RIGHT) {
        if (placement == CENTER || placement == TEXT_ONLY_RIGHT)
            y -= (measured_height / 2);
        else if (placement == BOTTOM_LEFT || placement == BOTTOM_RIGHT)
            y -= measured_height;
    }
    return twrpTruetype::gr_ttf_textExWH(gl, x, y + y_scale, s, vfont, measured_width + x, -1, gr_draw);
}

void gr_clip(int x, int y, int w, int h)
{
    gr_object_clip = display_rect(x, y, w, h);
    gr_object_clip_enabled = true;
    apply_render_clip();
}

void gr_noclip()
{
    gr_object_clip_enabled = false;
    apply_render_clip();
}

void gr_line(int x0, int y0, int x1, int y1, int width)
{
    GGLContext *gl = gr_context;
    int x0_disp, y0_disp, x1_disp, y1_disp;

    x0_disp = ROTATION_X_DISP(x0, y0, gr_draw->width);
    y0_disp = ROTATION_Y_DISP(x0, y0, gr_draw->height);
    x1_disp = ROTATION_X_DISP(x1, y1, gr_draw->width);
    y1_disp = ROTATION_Y_DISP(x1, y1, gr_draw->height);

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    const int coords0[2] = { x0_disp << 4, y0_disp << 4 };
    const int coords1[2] = { x1_disp << 4, y1_disp << 4 };
    gl->linex(gl, coords0, coords1, width << 4);
    const int half_width = (width + 1) / 2;
    gr_damage(std::min(x0_disp, x1_disp) - half_width,
              std::min(y0_disp, y1_disp) - half_width,
              std::max(x0_disp, x1_disp) + half_width + 1,
              std::max(y0_disp, y1_disp) + half_width + 1);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

gr_surface gr_render_circle(int radius, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    int rx, ry;
    GGLSurface *surface;
    const int diameter = radius*2 + 1;
    const int radius_check = radius*radius + radius*0.8;
    const uint32_t px = (a << 24) | (b << 16) | (g << 8) | r;
    uint32_t *data;

    surface = (GGLSurface *)malloc(sizeof(GGLSurface));
    memset(surface, 0, sizeof(GGLSurface));

    data = (uint32_t *)malloc(diameter * diameter * 4);
    memset(data, 0, diameter * diameter * 4);

    surface->version = sizeof(surface);
    surface->width = diameter;
    surface->height = diameter;
    surface->stride = diameter;
    surface->data = (GGLubyte*)data;
#if defined(RECOVERY_BGRA)
    surface->format = GGL_PIXEL_FORMAT_BGRA_8888;
#else
    surface->format = GGL_PIXEL_FORMAT_RGBA_8888;
#endif

    for(ry = -radius; ry <= radius; ++ry)
        for(rx = -radius; rx <= radius; ++rx)
            if(rx*rx+ry*ry <= radius_check)
                *(data + diameter*(radius + ry) + (radius+rx)) = px;

    return (gr_surface)surface;
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    GGLContext *gl = gr_context;
    GGLint color[4];
#if defined(RECOVERY_ARGB) || defined(RECOVERY_BGRA) || defined(RECOVERY_ABGR)
    color[0] = ((b << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((r << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
#else
    color[0] = ((r << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((b << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
#endif
    gl->color4xv(gl, color);

    gr_is_curr_clr_opaque = (a == 255);
}

void gr_clear()
{
    if (gr_draw->pixel_bytes == 2) {
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());
        return;
    }

    // This code only works on 32bpp devices
    if (gr_current_r == gr_current_g && gr_current_r == gr_current_b) {
        memset(gr_draw->data, gr_current_r, gr_draw->height * gr_draw->row_bytes);
    } else {
        unsigned char* px = gr_draw->data;
        for (int y = 0; y < gr_draw->height; ++y) {
            for (int x = 0; x < gr_draw->width; ++x) {
                *px++ = gr_current_r;
                *px++ = gr_current_g;
                *px++ = gr_current_b;
                px++;
            }
            px += gr_draw->row_bytes - (gr_draw->width * gr_draw->pixel_bytes);
        }
    }
    gr_damage(0, 0, gr_draw->width, gr_draw->height);
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;
    int x0_disp, y0_disp, x1_disp, y1_disp;
    int l_disp, r_disp, t_disp, b_disp;

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    x0_disp = ROTATION_X_DISP(x, y, gr_draw->width);
    y0_disp = ROTATION_Y_DISP(x, y, gr_draw->height);
    x1_disp = ROTATION_X_DISP(x + w, y + h, gr_draw->width);
    y1_disp = ROTATION_Y_DISP(x + w, y + h, gr_draw->height);
    l_disp = std::min(x0_disp, x1_disp);
    r_disp = std::max(x0_disp, x1_disp);
    t_disp = std::min(y0_disp, y1_disp);
    b_disp = std::max(y0_disp, y1_disp);
    gl->recti(gl, l_disp, t_disp, r_disp, b_disp);
    gr_damage(l_disp, t_disp, r_disp, b_disp);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy)
{
    if (gr_context == NULL) {
        return;
    }

    GGLContext *gl = gr_context;
    GGLSurface *surface = (GGLSurface*)source;

    if(surface->format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->disable(gl, GGL_BLEND);

    int dx0_disp, dy0_disp, dx1_disp, dy1_disp;
    int l_disp, r_disp, t_disp, b_disp;

    // Figuring out display coordinates works for gr_rotation == 0 too,
    // and isn't as expensive as allocating and rotating another surface,
    // so we do this anyway.
    dx0_disp = ROTATION_X_DISP(dx, dy, gr_draw->width);
    dy0_disp = ROTATION_Y_DISP(dx, dy, gr_draw->height);
    dx1_disp = ROTATION_X_DISP(dx + w, dy + h, gr_draw->width);
    dy1_disp = ROTATION_Y_DISP(dx + w, dy + h, gr_draw->height);
    l_disp = std::min(dx0_disp, dx1_disp);
    r_disp = std::max(dx0_disp, dx1_disp);
    t_disp = std::min(dy0_disp, dy1_disp);
    b_disp = std::max(dy0_disp, dy1_disp);

    GGLSurface surface_rotated;
    if (gr_rotation != 0) {
        // Do not perform relatively expensive operation if not needed
        surface_rotated.version = sizeof(surface_rotated);
        // Skip the **(gr_rotation == 0)** || (gr_rotation == 180) check
        // because we are under a gr_rotation != 0 conditional compilation statement
        surface_rotated.width   = (gr_rotation == 180) ? surface->width  : surface->height;
        surface_rotated.height  = (gr_rotation == 180) ? surface->height : surface->width;
        surface_rotated.stride  = surface_rotated.width;
        surface_rotated.format  = surface->format;
        surface_rotated.data    = (GGLubyte*) malloc(surface_rotated.stride * surface_rotated.height * 4);
        surface_ROTATION_transform((gr_surface) &surface_rotated, (const gr_surface) surface, 4);

        gl->bindTexture(gl, &surface_rotated);
    } else {
        gl->bindTexture(gl, surface);
    }

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, sx - l_disp, sy - t_disp);
    gl->recti(gl, l_disp, t_disp, r_disp, b_disp);
    gr_damage(l_disp, t_disp, r_disp, b_disp);
    gl->disable(gl, GGL_TEXTURE_2D);

    if (gr_rotation != 0)
        free(surface_rotated.data);

    if(surface->format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->enable(gl, GGL_BLEND);
}

int gr_blit_raw(const void* data, int width, int height, int row_bytes, int dx, int dy)
{
    if (!gr_draw || !data || width <= 0 || height <= 0 || row_bytes < width * 4)
        return -1;

    if (gr_target_format == GRPixelFormat::UNKNOWN)
        return -1;

    // LVGL's XRGB8888 is 0xXXRRGGBB, therefore its little-endian bytes are
    // B,G,R,X. Convert directly into minui's current draw buffer. This avoids
    // the pixelflinger texture path, which is needlessly expensive for a
    // completed LVGL raster buffer and was also unable to fix channel order.
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
        gr_raw_native_frame = true;
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
        gr_raw_native_frame = true;
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
        gr_raw_native_frame = true;
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
    gr_raw_native_frame = true;
    return 0;
}

unsigned int gr_get_width(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->width;
}

unsigned int gr_get_height(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->height;
}

unsigned int gr_get_row_bytes(gr_surface surface) {
    if (surface == NULL)
        return 0;
    return static_cast<unsigned int>(((GGLSurface*)surface)->stride * gr_draw->pixel_bytes);
}

const unsigned char* gr_get_data(gr_surface surface) {
    if (surface == NULL)
        return NULL;
    return ((GGLSurface*)surface)->data;
}

void gr_flip() {
    gr_draw = gr_backend->flip(gr_backend);
    gr_raw_frame_done();
    gr_reset_damage();
    // On double buffered back ends, when we flip, we need to tell
    // pixel flinger to draw to the other buffer
    gr_mem_surface.data = (GGLubyte*)gr_draw->data;
    gr_context->colorBuffer(gr_context, &gr_mem_surface);
}

static void get_memory_surface(GGLSurface* ms) {
    ms->version = sizeof(*ms);
    ms->width = gr_draw->width;
    ms->height = gr_draw->height;
    ms->stride = gr_draw->row_bytes / gr_draw->pixel_bytes;
    ms->data = (GGLubyte*)gr_draw->data;
    ms->format = gr_draw->format;
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

    // Set up pixelflinger
    get_memory_surface(&gr_mem_surface);
    gglInit(&gr_context);
    GGLContext *gl = gr_context;
    gl->colorBuffer(gl, &gr_mem_surface);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

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

int gr_get_surface(gr_surface* surface)
{
    GGLSurface* ms = (GGLSurface*)malloc(sizeof(GGLSurface));
    if (!ms)    return -1;

    // Allocate the data
    get_memory_surface(ms);
    ms->data = (GGLubyte*)malloc(ms->stride * ms->height * gr_draw->pixel_bytes);

    // Now, copy the data
    memcpy(ms->data, gr_mem_surface.data, gr_draw->width * gr_draw->height * gr_draw->pixel_bytes / 8);

    *surface = (gr_surface*) ms;
    return 0;
}

int gr_free_surface(gr_surface surface)
{
    if (!surface)
        return -1;

    GGLSurface* ms = (GGLSurface*) surface;
    free(ms->data);
    free(ms);
    return 0;
}

void gr_write_frame_to_file(int fd)
{
    write(fd, gr_mem_surface.data, gr_draw->width * gr_draw->height * gr_draw->pixel_bytes / 8);
}
