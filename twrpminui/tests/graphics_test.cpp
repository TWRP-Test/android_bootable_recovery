#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <png.h>
#include "graphics.h"

namespace {
GRSurface surface{};
std::vector<unsigned char> pixels;
unsigned int flips;
GRSurface* init(minui_backend*) { return &surface; }
GRSurface* flip(minui_backend*) { ++flips; return &surface; }
void blank(minui_backend*, bool) {}
void exit_backend(minui_backend*) {}
minui_backend backend{init, flip, blank, exit_backend};
using RGB = std::array<unsigned char, 3>;
constexpr RGB colors[] = {{255,0,0}, {0,255,0}, {0,0,255},
                          {255,255,255}, {0,0,0}, {255,255,0}};
}
minui_backend* open_fbdev() { return &backend; }
extern unsigned int gr_rotation;
int property_get(const char*, char* value, const char* fallback) {
    std::strcpy(value, fallback);
    return std::strlen(value);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    for (auto format : {GRPixelFormat::RGB565, GRPixelFormat::RGBA8888,
                        GRPixelFormat::RGBX8888, GRPixelFormat::BGRA8888,
                        GRPixelFormat::ABGR8888, GRPixelFormat::ARGB8888,
                        GRPixelFormat::XRGB8888}) {
        for (unsigned int rotation : {0, 90, 180, 270}) {
            const int bpp = format == GRPixelFormat::RGB565 ? 2 : 4;
            pixels.assign(2 * (3 * bpp + 8), 0x5a);
            surface = {3, 2, 3 * bpp + 8, bpp, pixels.data(), format};
            flips = 0;
            assert(gr_init() == 0 && flips == 2);
            gr_set_pixel_format(format);
            gr_rotation = rotation;
            const int width = gr_fb_width(), height = gr_fb_height();
            std::vector<unsigned char> source(width * height * 4);
            for (int i = 0; i < width * height; ++i) {
                source[i*4] = colors[i][2]; source[i*4+1] = colors[i][1];
                source[i*4+2] = colors[i][0]; source[i*4+3] = 255;
            }
            assert(gr_blit_raw(source.data(), width, height, width*4, 0, 0) == 0);
            auto damage = gr_get_damage();
            assert(damage.left == 0 && damage.top == 0 && damage.right == 3 && damage.bottom == 2);
            for (int y = 0; y < 2; ++y)
                for (int x = 3*bpp; x < surface.row_bytes; ++x)
                    assert(pixels[y*surface.row_bytes+x] == 0x5a);
            assert(gr_save_screenshot(argv[1]) == 0);
            png_image image{};
            image.version = PNG_IMAGE_VERSION;
            assert(png_image_begin_read_from_file(&image, argv[1]));
            assert(image.width == static_cast<unsigned>(width) && image.height == static_cast<unsigned>(height));
            image.format = PNG_FORMAT_RGB;
            std::vector<unsigned char> decoded(PNG_IMAGE_SIZE(image));
            assert(png_image_finish_read(&image, nullptr, decoded.data(), 0, nullptr));
            for (int i = 0; i < width * height; ++i)
                for (int c = 0; c < 3; ++c) assert(decoded[i*3+c] == colors[i][c]);
            png_image_free(&image);
            gr_flip();
            damage = gr_get_damage();
            assert(flips == 3 && damage.left == damage.right && damage.top == damage.bottom);
            assert(gr_blit_raw(source.data(), width, height, 1, 0, 0) == -1);
            gr_exit();
            assert(gr_save_screenshot(argv[1]) == -1);
        }
    }
    std::remove(argv[1]);
    std::puts("PASS: 7 formats x 4 rotations, padded rows, PNG round trips, flip/damage, invalid input");
}
