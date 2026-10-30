/*
 * Copyright (C) 2014 The Android Open Source Project
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

// Baseline: current minui/graphics.h; local differences are marked TWRP.

#ifndef _GRAPHICS_H_
#define _GRAPHICS_H_

#include "twrpminui/minui.h"

// TWRP: physical damage bounds for LVGL partial presentation.
struct GRRect {
    int left;
    int top;
    int right;
    int bottom;
};


class MinuiBackend {
 public:
  enum DrmConnector {
    DRM_MAIN = 0,
    DRM_SEC,
    DRM_MAX,
  };

  // Initializes the backend and returns a GRSurface* to draw into.
  virtual GRSurface* Init() = 0;

  // Causes the current drawing surface (returned by the most recent call to Flip() or Init()) to
  // be displayed, and returns a new drawing surface.
  virtual GRSurface* Flip() = 0;

  // Blank (or unblank) the default screen.
  virtual void Blank(bool) = 0;

  // Blank (or unblank) the specific screen.
  virtual void Blank(bool blank, DrmConnector index) = 0;

  // Return true if the device supports multiple connectors.
  virtual bool HasMultipleConnectors() = 0;

  // Device cleanup when drawing is done.
  virtual ~MinuiBackend() = default;
};

// TWRP: backend metadata and damage tracking (aff6ddb2, b7457030).
void gr_damage(int left, int top, int right, int bottom);
GRRect gr_get_damage();
void gr_reset_damage();
void gr_set_pixel_format(GRPixelFormat format);
void gr_set_physical_size(int width_mm, int height_mm);
struct fb_var_screeninfo;
GRPixelFormat gr_framebuffer_format(const fb_var_screeninfo& info, GRPixelFormat fallback);
std::unique_ptr<MinuiBackend> create_backend(GraphicsBackend backend);

#endif  // _GRAPHICS_H_
