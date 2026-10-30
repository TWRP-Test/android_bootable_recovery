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

// Baseline: current minui/graphics_fbdev.cpp; local differences are marked TWRP.

#include "graphics_fbdev.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <algorithm>

#include <android-base/unique_fd.h>

#include "twrpminui/minui.h"

std::unique_ptr<GRSurfaceFbdev> GRSurfaceFbdev::Create(size_t width, size_t height,
                                                       size_t row_bytes, size_t pixel_bytes) {
  // Cannot use std::make_unique to access non-public ctor.
  return std::unique_ptr<GRSurfaceFbdev>(new GRSurfaceFbdev(width, height, row_bytes, pixel_bytes));
}

// TWRP: brightness fallback and secondary brightness path (2948b6f3).
void MinuiBackendFbdev::Blank(bool blank) {
#if defined(TW_NO_SCREEN_BLANK) && defined(TW_BRIGHTNESS_PATH) && defined(TW_MAX_BRIGHTNESS)
    int fd;
    char brightness[4];
    snprintf(brightness, 4, "%03d", TW_MAX_BRIGHTNESS/2);

    fd = open(TW_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        perror("cannot open LCD backlight");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);

#ifdef TW_SECONDARY_BRIGHTNESS_PATH
    fd = open(TW_SECONDARY_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        perror("cannot open LCD backlight 2");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);
#endif
#else
#ifndef TW_NO_SCREEN_BLANK
    int ret;

    ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
#endif
#endif
}

void MinuiBackendFbdev::Blank(bool blank, DrmConnector index) {
  if (index == DRM_MAIN) {
    MinuiBackendFbdev::Blank(blank);
  } else {
    fprintf(stderr, "Unsupported multiple connectors, blank = %d, index = %d\n", blank, index);
  }
}

bool MinuiBackendFbdev::HasMultipleConnectors() {
  fprintf(stderr, "Unsupported multiple connectors\n");
  return false;
}

void MinuiBackendFbdev::SetDisplayedFramebuffer(size_t n) {
  if (n > 1 || !double_buffered) return;

  vi.yres_virtual = gr_framebuffer[0]->height * 2;
  vi.yoffset = n * gr_framebuffer[0]->height;
  vi.bits_per_pixel = gr_framebuffer[0]->pixel_bytes * 8;
  if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
    // TWRP: HiSilicon kernels require FBIOPAN_DISPLAY (0ac2293a).
    if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vi) < 0) perror("active fb swap failed");
  }
  displayed_buffer = n;
}

GRSurface* MinuiBackendFbdev::Init() {
  // TWRP: wait briefly for init to create fb0 (b9d8f62c).
  android::base::unique_fd fd;
  for (int attempt = 0; attempt < 20 && fd < 0; ++attempt) {
    fd.reset(open("/dev/graphics/fb0", O_RDWR | O_CLOEXEC));
    if (fd < 0 && attempt != 19) usleep(100000);
  }
  if (fd < 0) return nullptr;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) return nullptr;
  gr_set_physical_size(static_cast<int>(vi.width), static_cast<int>(vi.height));
  // TWRP: forcing RGB565 must precede querying the fixed framebuffer layout.
#ifdef RECOVERY_FORCE_RGB_565
    // Changing fb_var_screeninfo can affect fb_fix_screeninfo,
    // so this needs done before querying for fi.
    printf("Forcing pixel format: RGB_565\n");
    vi.blue.offset    = 0;
    vi.green.offset   = 5;
    vi.red.offset     = 11;
    vi.blue.length    = 5;
    vi.green.length   = 6;
    vi.red.length     = 5;
    vi.blue.msb_right = 0;
    vi.green.msb_right = 0;
    vi.red.msb_right = 0;
    vi.transp.offset  = 0;
    vi.transp.length  = 0;
    vi.bits_per_pixel = 16;

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("failed to put force_rgb_565 fb0 info");
        return nullptr;
    }
#endif


  fb_fix_screeninfo fi;
  if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) return nullptr;
  // We print this out for informational purposes only, but
  // throughout we assume that the framebuffer device uses an RGBX
  // pixel format.  This is the case for every development device I
  // have access to.  For some of those devices (eg, hammerhead aka
  // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
  // different format (XBGR) but actually produces the correct
  // results on the display when you write RGBX.
  //
  // If you have a device that actually *needs* another pixel format
  // (ie, BGRX, or 565), patches welcome...

  printf(
      "fb0 reports (possibly inaccurate):\n"
      "  vi.bits_per_pixel = %d\n"
      "  vi.red.offset   = %3d   .length = %3d\n"
      "  vi.green.offset = %3d   .length = %3d\n"
      "  vi.blue.offset  = %3d   .length = %3d\n",
      vi.bits_per_pixel, vi.red.offset, vi.red.length, vi.green.offset, vi.green.length,
      vi.blue.offset, vi.blue.length);

  void* bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (bits == MAP_FAILED) {
    perror("failed to mmap framebuffer");
    return nullptr;
  }

  // TWRP: retain mapping ownership even if later surface allocation fails.
  mapping = bits;
  mapping_size = fi.smem_len;
  memset(bits, 0, fi.smem_len);

  gr_framebuffer[0] =
      GRSurfaceFbdev::Create(vi.xres, vi.yres, fi.line_length, vi.bits_per_pixel / 8);
  gr_framebuffer[0]->buffer_ = static_cast<uint8_t*>(bits);
  memset(gr_framebuffer[0]->buffer_, 0, gr_framebuffer[0]->height * gr_framebuffer[0]->row_bytes);

  gr_framebuffer[1] =
      GRSurfaceFbdev::Create(gr_framebuffer[0]->width, gr_framebuffer[0]->height,
                             gr_framebuffer[0]->row_bytes, gr_framebuffer[0]->pixel_bytes);

  // TWRP: explicit output byte order is used by LVGL and PNG capture.
  gr_set_pixel_format(gr_framebuffer_format(vi, gr_frame_pixel_format()));
#ifdef RECOVERY_GRAPHICS_FORCE_USE_LINELENGTH
  vi.xres_virtual = fi.line_length / gr_framebuffer[0]->pixel_bytes;
#endif
  /* check if we can use double buffering */
#ifndef RECOVERY_GRAPHICS_FORCE_SINGLE_BUFFER
  if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
#else
  if (false) { // TWRP: 4a71d4f6, force single-buffer scanout.
#endif
    double_buffered = true;

    gr_framebuffer[1]->buffer_ =
        gr_framebuffer[0]->buffer_ + gr_framebuffer[0]->height * gr_framebuffer[0]->row_bytes;
  } else {
    double_buffered = false;

    // Without double-buffering, we allocate RAM for a buffer to draw in, and then "flipping" the
    // buffer consists of a memcpy from the buffer we allocated to the framebuffer.
    memory_buffer.resize(gr_framebuffer[1]->height * gr_framebuffer[1]->row_bytes);
    gr_framebuffer[1]->buffer_ = memory_buffer.data();
  }

  gr_draw = gr_framebuffer[1].get();
  memset(gr_draw->buffer_, 0, gr_draw->height * gr_draw->row_bytes);
  fb_fd = std::move(fd);
  SetDisplayedFramebuffer(0);

  printf("framebuffer: %d (%zu x %zu)\n", fb_fd.get(), gr_draw->width, gr_draw->height);
  Blank(false);

  // TWRP: minui's scanout surfaces stay backend-owned; LVGL draws into stable RAM.
  staging_buffer = GRSurface::Create(gr_draw->width, gr_draw->height,
                                     gr_draw->row_bytes, gr_draw->pixel_bytes);
  if (!staging_buffer) return nullptr;
  memset(staging_buffer->data(), 0, staging_buffer->row_bytes * staging_buffer->height);
  return staging_buffer.get();
}

#if 0 // TWRP: unmodified minui reference; retained for future resyncs.
GRSurface* MinuiBackendFbdev::Flip() {
  if (double_buffered) {
    // Change gr_draw to point to the buffer currently displayed, then flip the driver so we're
    // displaying the other buffer instead.
    gr_draw = gr_framebuffer[displayed_buffer].get();
    SetDisplayedFramebuffer(1 - displayed_buffer);
  } else {
    // Copy from the in-memory surface to the framebuffer.
    memcpy(gr_framebuffer[0]->buffer_, gr_draw->buffer_, gr_draw->height * gr_draw->row_bytes);
  }
  return gr_draw;
}
#else // TWRP: active implementation below preserves GUI2/device requirements.
GRSurface* MinuiBackendFbdev::Flip() {
  // TWRP: preserve partial LVGL updates while scanout changes buffers.
  const int target = double_buffered ? 1 - displayed_buffer : 0;
  memcpy(gr_framebuffer[target]->buffer_, staging_buffer->data(),
         staging_buffer->height * staging_buffer->row_bytes);
  if (double_buffered) SetDisplayedFramebuffer(target);
  return staging_buffer.get();
}
#endif // TWRP


// TWRP: minui's unique_fd closes the device; the mmap needs explicit ownership.
MinuiBackendFbdev::~MinuiBackendFbdev() {
  if (mapping) munmap(mapping, mapping_size);
}
