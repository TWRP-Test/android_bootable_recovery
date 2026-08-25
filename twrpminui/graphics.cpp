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

int gr_upload_pixels(int x, int y, int w, int h, int stride_bytes, const void* pixels, int pixel_bytes)
{
    if (!gr_draw || !pixels || w <= 0 || h <= 0)
        return -1;

    if (gr_rotation != 0)
        return -1;

    if (x < 0 || y < 0 || x + w > gr_draw->width || y + h > gr_draw->height)
        return -1;
    if (stride_bytes < w * pixel_bytes)
        return -1;

    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    const int target_format = gr_get_pixel_format();
    if (target_format == GR_PIXEL_FORMAT_RGB565 && pixel_bytes == 2 &&
        gr_draw->pixel_bytes == 2) {
        const size_t row_bytes = static_cast<size_t>(w) * pixel_bytes;
        for (int row = 0; row < h; ++row)
            memcpy(gr_draw->data + (y + row) * gr_draw->row_bytes + x * pixel_bytes,
                   src + row * stride_bytes, row_bytes);
    } else if (target_format == GR_PIXEL_FORMAT_RGB565 && pixel_bytes == 4 &&
               gr_draw->pixel_bytes == 2) {
        // Convert the LVGL source pixels to the target RGB565 layout.
        for (int row = 0; row < h; ++row) {
            const uint8_t* src_row = src + row * stride_bytes;
            uint16_t* dst_row = reinterpret_cast<uint16_t*>(
                gr_draw->data + (y + row) * gr_draw->row_bytes + x * 2);
            for (int col = 0; col < w; ++col) {
                const uint8_t* px = src_row + col * 4;
                const uint16_t r = px[2] >> 3;
                const uint16_t g = px[1] >> 2;
                const uint16_t b = px[0] >> 3;
                dst_row[col] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
            }
        }
    } else if (pixel_bytes == 4 && gr_draw->pixel_bytes == 4 &&
               target_format == GR_PIXEL_FORMAT_BGRA8888) {
        // The source and target use the same four-byte memory layout.
        const size_t row_bytes = static_cast<size_t>(w) * 4;
        for (int row = 0; row < h; ++row)
            memcpy(gr_draw->data + (y + row) * gr_draw->row_bytes + x * 4,
                   src + row * stride_bytes, row_bytes);
    } else if (pixel_bytes == 4 && gr_draw->pixel_bytes == 4) {
        // Reorder LVGL's bytes to the selected recovery format.
        for (int row = 0; row < h; ++row) {
            const uint8_t* src_row = src + row * stride_bytes;
            uint8_t* dst_row = gr_draw->data + (y + row) * gr_draw->row_bytes + x * 4;
            for (int col = 0; col < w; ++col) {
                const uint8_t* s = src_row + col * 4;
                uint8_t* d = dst_row + col * 4;
                switch (target_format) {
                    case GR_PIXEL_FORMAT_RGBA8888:
                        d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
                        break;
                    case GR_PIXEL_FORMAT_RGBX8888:
                        d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0xFF;
                        break;
                    case GR_PIXEL_FORMAT_ABGR8888:
                        d[0] = s[3]; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
                        break;
                    default:
                        return -1;
                }
            }
        }
    } else {
        return -1;
    }

    gr_damage(x, y, x + w, y + h);
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

void gr_flip() {
    gr_draw = gr_backend->flip(gr_backend);
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

static int init_backend()
{
    gr_draw = NULL;

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

    return 0;
}

int gr_init_display(void)
{
    if (init_backend() != 0)
        return -1;
    gr_draw = gr_backend->flip(gr_backend);
    gr_draw = gr_backend->flip(gr_backend);
    return 0;
}

int gr_init(void)
{
    if (init_backend() != 0)
        return -1;

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
    gr_backend->exit(gr_backend);
}

void gr_flip_display(void)
{
    gr_draw = gr_backend->flip(gr_backend);
    gr_reset_damage();
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

int gr_get_pixel_format(void)
{
    if (!gr_draw)
        return 0;
    if (gr_draw->pixel_bytes == 2)
        return GR_PIXEL_FORMAT_RGB565;
#if defined(RECOVERY_ABGR)
    // Match the byte layout selected by graphics_drm.
    return GR_PIXEL_FORMAT_ABGR8888;
#elif defined(RECOVERY_RGBA)
    return GR_PIXEL_FORMAT_RGBA8888;
#elif defined(RECOVERY_RGBX)
    return GR_PIXEL_FORMAT_RGBX8888;
#elif defined(RECOVERY_BGRA)
    return GR_PIXEL_FORMAT_BGRA8888;
#else
    return GR_PIXEL_FORMAT_RGBX8888;
#endif
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
