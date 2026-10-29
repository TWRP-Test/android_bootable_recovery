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
#include <stdint.h>

// Names describe byte order in memory, except packed little-endian RGB565.
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

struct GRSurface {
  int width;
  int height;
  int row_bytes;
  int pixel_bytes;
  unsigned char* data;
  GRPixelFormat format;
};

int gr_init(void);
void gr_exit(void);

int gr_fb_width(void);
int gr_fb_height(void);
// Panel size in millimetres, oriented like gr_fb_width/height; false when unknown.
bool gr_fb_physical_size(int* width_mm, int* height_mm);
int gr_copy_frame(void* destination, size_t capacity, int* width, int* height, int* row_bytes,
                  GRPixelFormat* format);
GRPixelFormat gr_pixel_format(void);
void gr_flip(void);
void gr_fb_blank(bool blank);

int gr_blit_raw(const void* data, int width, int height, int row_bytes, int dx, int dy);
int gr_fb_pixel_bytes(void);

// PNG capture in logical display orientation.
int gr_save_screenshot(const char* dest);

// Transform twrpminui API coordinates into display coordinates,
// for panels that are hardware-mounted in a rotated manner.
int ROTATION_X_DISP(int x, int y, int w);

int ROTATION_Y_DISP(int x, int y, int h);

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
int ev_has_keyboard(void);
// Whether the last event returned by ev_get came from a physical keyboard.
bool ev_input_is_keyboard(void);

// Return the next complete multitouch frame assembled by ev_get().
int twrp_input_take_touch_events(TWRPTouchPoint* points, int max_points);

int vibrate(int timeout_ms);
int haptics_available();

#endif
