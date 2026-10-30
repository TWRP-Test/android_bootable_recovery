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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits>
#include <vector>
#include <png.h>
#include <linux/fb.h>
#include "graphics.h"

#include "twrpminui/minui.h"

// TWRP: PNG capture and hardware panel rotation (02827311).

int gr_save_screenshot(const char* dest)
{
    int width, height, row_bytes;
    GRPixelFormat format;
    if (!dest || gr_copy_frame(nullptr, 0, &width, &height, &row_bytes, &format) != -2 ||
        width <= 0 || height <= 0 || row_bytes <= 0 || format == GRPixelFormat::UNKNOWN)
        return -1;

    const int pixel_bytes = format == GRPixelFormat::RGB565 ? 2 : 4;
    if (static_cast<size_t>(width) > static_cast<size_t>(row_bytes) / pixel_bytes ||
        static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / row_bytes)
        return -1;

    // Snapshot before encoding so row padding and the physical panel layout
    // remain explicit. The output follows the logical display orientation.
    std::vector<uint8_t> frame(static_cast<size_t>(height) * row_bytes);
    if (gr_copy_frame(frame.data(), frame.size(), &width, &height, &row_bytes, &format) != 0)
        return -1;
    const unsigned int rotation = static_cast<unsigned int>(gr_get_rotation()) * 90;
    const bool sideways = rotation == 90 || rotation == 270;
    const int output_width = sideways ? height : width;
    const int output_height = sideways ? width : height;
    if (static_cast<size_t>(output_width) > std::numeric_limits<size_t>::max() / 3)
        return -1;
    std::vector<uint8_t> row(static_cast<size_t>(output_width) * 3);

    FILE* fp = fopen(dest, "wb");
    if (!fp)
        return -1;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return -1;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return -1;
    }
    // All C++ objects above predate setjmp; no destructors are skipped by libpng.
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        remove(dest);
        return -1;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, output_width, output_height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);
    for (int y = 0; y < output_height; ++y) {
        for (int x = 0; x < output_width; ++x) {
            int px = x, py = y;
            switch (rotation) {
                case 90: px = width - y - 1; py = x; break;
                case 180: px = width - x - 1; py = height - y - 1; break;
                case 270: px = y; py = height - x - 1; break;
            }
            const uint8_t* source = frame.data() + static_cast<size_t>(py) * row_bytes +
                                    static_cast<size_t>(px) * pixel_bytes;
            uint8_t* rgb = row.data() + static_cast<size_t>(x) * 3;
            switch (format) {
                case GRPixelFormat::RGB565: {
                    const uint16_t pixel = source[0] | (static_cast<uint16_t>(source[1]) << 8);
                    const uint8_t r = (pixel >> 11) & 31;
                    const uint8_t g = (pixel >> 5) & 63;
                    const uint8_t b = pixel & 31;
                    rgb[0] = (r << 3) | (r >> 2);
                    rgb[1] = (g << 2) | (g >> 4);
                    rgb[2] = (b << 3) | (b >> 2);
                    break;
                }
                case GRPixelFormat::RGBA8888:
                case GRPixelFormat::RGBX8888:
                    rgb[0] = source[0]; rgb[1] = source[1]; rgb[2] = source[2]; break;
                case GRPixelFormat::BGRA8888:
                    rgb[0] = source[2]; rgb[1] = source[1]; rgb[2] = source[0]; break;
                case GRPixelFormat::ABGR8888:
                    rgb[0] = source[3]; rgb[1] = source[2]; rgb[2] = source[1]; break;
                case GRPixelFormat::ARGB8888:
                case GRPixelFormat::XRGB8888:
                    rgb[0] = source[1]; rgb[1] = source[2]; rgb[2] = source[3]; break;
                default:
                    png_error(png, "Unsupported framebuffer pixel format");
            }
        }
        png_write_row(png, row.data());
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    if (fclose(fp) != 0) {
        remove(dest);
        return -1;
    }
    return 0;
}

int ROTATION_X_DISP(int x, int y, int w) {
    return ((static_cast<unsigned int>(gr_get_rotation()) * 90 ==   0) ? (x) :
            (static_cast<unsigned int>(gr_get_rotation()) * 90 ==  90) ? (w - (y) - 1) :
            (static_cast<unsigned int>(gr_get_rotation()) * 90 == 180) ? (w - (x) - 1) :
            (static_cast<unsigned int>(gr_get_rotation()) * 90 == 270) ? (y) : -1);
}

int ROTATION_Y_DISP(int x, int y, int h) {
    return ((static_cast<unsigned int>(gr_get_rotation()) * 90 ==   0) ? (y) :
            (static_cast<unsigned int>(gr_get_rotation()) * 90 ==  90) ? (x) :
            (static_cast<unsigned int>(gr_get_rotation()) * 90 == 180) ? (h - (y) - 1) :
            (static_cast<unsigned int>(gr_get_rotation()) * 90 == 270) ? (h - (x) - 1) : -1);
}

// TWRP: actual mmap byte order, used by fbdev (b7457030).
GRPixelFormat gr_framebuffer_format(const fb_var_screeninfo& vi, GRPixelFormat fallback)
{
    if (vi.bits_per_pixel == 16)
        return GRPixelFormat::RGB565;

    if (vi.bits_per_pixel != 32 || vi.red.length != 8 ||
        vi.green.length != 8 || vi.blue.length != 8)
        return fallback;

    // fb_var_screeninfo offsets describe the bit layout of the actual
    // mmap()ed bytes. Do not infer this from red.offset alone: ARGB and
    // BGRA, for example, overlap on one channel offset.
    if (vi.red.offset == 0 && vi.green.offset == 8 && vi.blue.offset == 16)
        return vi.transp.length == 8 ? GRPixelFormat::RGBA8888
                                     : GRPixelFormat::RGBX8888;
    if (vi.blue.offset == 0 && vi.green.offset == 8 && vi.red.offset == 16)
        return GRPixelFormat::BGRA8888;
    if (vi.red.offset == 24 && vi.green.offset == 16 && vi.blue.offset == 8)
        return vi.transp.length == 8 && vi.transp.offset == 0
                ? GRPixelFormat::ABGR8888 : GRPixelFormat::XRGB8888;
    if (vi.blue.offset == 24 && vi.green.offset == 16 && vi.red.offset == 8)
        return GRPixelFormat::ARGB8888;
    return fallback;
}
