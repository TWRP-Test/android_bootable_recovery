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

#ifndef _MINUI_H_
#define _MINUI_H_

#include <stdbool.h>
#include <stddef.h>
#include "gui/placement.h"

struct GRSurface {
  int width;
  int height;
  int row_bytes;
  int pixel_bytes;
  unsigned char* data;
  __u32 format;
};

typedef void* gr_surface;
typedef unsigned short gr_pixel;

enum class GRPixelFormat : int {
  UNKNOWN = 0,
  RGB565,
  RGBA8888,
  RGBX8888,
  BGRA8888,
  ABGR8888,
  ARGB8888,
  XRGB8888,
};

#define FONT_TYPE_TWRP 0
#define FONT_TYPE_TTF 1

int gr_init(void);
void gr_exit(void);

int gr_fb_width(void);
int gr_fb_height(void);
int gr_copy_frame(void* destination, size_t capacity, int* width, int* height, int* row_bytes,
                  GRPixelFormat* format);
GRPixelFormat gr_pixel_format(void);
gr_pixel* gr_fb_data(void);
void gr_flip(void);
void gr_fb_blank(bool blank);

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void gr_invalidate(int x, int y, int w, int h);
bool gr_begin_damage_clip();
void gr_end_damage_clip();
void gr_clip(int x, int y, int w, int h);
void gr_noclip();
void gr_fill(int x, int y, int w, int h);
void gr_line(int x0, int y0, int x1, int y1, int width);
gr_surface gr_render_circle(int radius, unsigned char r, unsigned char g, unsigned char b,
                            unsigned char a);

int gr_textEx_scaleW(int x, int y, const char* s, void* pFont, int max_width, int placement,
                     int scale);

int gr_getMaxFontHeight(void* font);

void* gr_ttf_loadFont(const char* filename, int size, int dpi);
void* gr_ttf_scaleFont(void* font, int max_width, int measured_width);
void gr_ttf_freeFont(void* font);
int gr_ttf_textExWH(void* context, int x, int y, const char* s, void* pFont, int max_width,
                    int max_height, const gr_surface gr_draw);
int gr_ttf_measureEx(const char* s, void* font);
int gr_ttf_maxExW(const char* s, void* font, int max_width);
int gr_ttf_getMaxFontHeight(void* font);
void gr_ttf_dump_stats(void);

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy);
int gr_blit_raw(const void* data, int width, int height, int row_bytes, int dx, int dy);
int gr_fb_pixel_bytes(void);
unsigned int gr_get_width(gr_surface surface);
unsigned int gr_get_height(gr_surface surface);
unsigned int gr_get_row_bytes(gr_surface surface);
const unsigned char* gr_get_data(gr_surface surface);
int gr_get_surface(gr_surface* surface);
int gr_free_surface(gr_surface surface);

// Functions in graphics_utils.c
int gr_save_screenshot(const char* dest);

// Transform twrpminui API coordinates into display coordinates,
// for panels that are hardware-mounted in a rotated manner.
int ROTATION_X_DISP(int x, int y, int w);

int ROTATION_Y_DISP(int x, int y, int h);

void surface_ROTATION_transform(gr_surface dst_ptr, const gr_surface src_ptr,
                                size_t num_bytes_per_pixel);

// input event structure, include <linux/input.h> for the definition.
// see http://www.mjmwired.net/kernel/Documentation/input/ for info.
struct input_event;

// Synthetic EV_ABS code emitted by ev_get() for absolute pointer devices
// such as QEMU's usb-tablet. Its value packs screen x/y like the legacy
// touch event, but it never represents a touch press or release.
#define TWRP_ABS_MOUSE_POSITION 0x3f

// Maximum number of simultaneous contacts exposed by the raw multitouch API.
// LVGL currently uses the first two contacts for gesture recognition.
#define TWRP_MAX_TOUCH_POINTS 16
#define TWRP_MAX_TOUCH_EVENTS (TWRP_MAX_TOUCH_POINTS * 2)

struct TWRPTouchPoint {
  int id;  // Stable Type-B slot ID, or a best-effort Type-A ID.
  int x;
  int y;
  bool pressed;  // false means that this contact was released.
};

int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event* ev, int timeout_ms);
int ev_has_mouse(void);

// Return the next complete multitouch frame assembled by ev_get().
int twrp_input_take_touch_events(TWRPTouchPoint* points, int max_points);

// Resources

// Returns 0 if no error, else negative.
int res_create_surface(const char* name, gr_surface* pSurface);
void res_free_surface(gr_surface surface);
int res_scale_surface(gr_surface source, gr_surface* destination, float scale_w, float scale_h);

int vibrate(int timeout_ms);
int haptics_available();

#endif
