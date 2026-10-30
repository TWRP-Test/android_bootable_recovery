#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <png.h>
#include "graphics_fbdev.h"
#include "graphics_drm.h"
#include <android-base/properties.h>

namespace {
std::unique_ptr<GRSurface> surface;
unsigned int flips;
bool fail_init = false;
unsigned int backend_exits = 0;
using RGB = std::array<unsigned char, 3>;
constexpr RGB colors[] = {{255,0,0}, {0,255,0}, {0,0,255},
                          {255,255,255}, {0,0,0}, {255,255,0}};
}
// Exercise minui's public Init/Flip/lifetime contract without a display device.
GRSurface* MinuiBackendFbdev::Init() { return fail_init ? nullptr : surface.get(); }
GRSurface* MinuiBackendFbdev::Flip() { ++flips; return surface.get(); }
void MinuiBackendFbdev::Blank(bool) {}
void MinuiBackendFbdev::Blank(bool, DrmConnector) {}
bool MinuiBackendFbdev::HasMultipleConnectors() { return false; }
MinuiBackendFbdev::~MinuiBackendFbdev() { ++backend_exits; }

// No DRM device on the host: exercise the production DRM-to-fbdev fallback.
GRSurface* MinuiBackendDrm::Init() { return nullptr; }
GRSurface* MinuiBackendDrm::Flip() { return nullptr; }
void MinuiBackendDrm::Blank(bool) {}
void MinuiBackendDrm::Blank(bool, DrmConnector) {}
bool MinuiBackendDrm::HasMultipleConnectors() { return false; }
MinuiBackendDrm::~MinuiBackendDrm() = default;
GRSurfaceDrm::~GRSurfaceDrm() = default;

namespace android::base {
std::string pixel_format_property;
std::map<std::string, std::string> properties;
std::string GetProperty(const std::string& key, const std::string& fallback) {
    if (properties.count(key)) return properties[key];
    return key == "ro.minui.pixel_format" && !pixel_format_property.empty()
        ? pixel_format_property : fallback;
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    for (auto format : {GRPixelFormat::RGB565, GRPixelFormat::RGBA8888,
                        GRPixelFormat::RGBX8888, GRPixelFormat::BGRA8888,
                        GRPixelFormat::ABGR8888, GRPixelFormat::ARGB8888,
                        GRPixelFormat::XRGB8888}) {
        for (unsigned int rotation : {0, 90, 180, 270}) {
            const int bpp = format == GRPixelFormat::RGB565 ? 2 : 4;
            surface = GRSurface::Create(3, 2, 3*bpp+8, bpp);
            assert(surface);
            memset(surface->data(), 0x5a, surface->data_size());
            flips = 0;
            assert(gr_init() == 0 && flips == 2);
            gr_set_pixel_format(format);
            gr_rotate(static_cast<GRRotation>(rotation / 90));
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
                for (int x = 3*bpp; x < static_cast<int>(surface->row_bytes); ++x)
                    assert(surface->data()[y*surface->row_bytes+x] == 0x5a);
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
    // Native minui APIs remain available, including resource loading and RAII clones.
    surface = GRSurface::Create(3, 2, 20, 4);
    android::base::pixel_format_property = "RGBX_8888";
    assert(gr_init({GraphicsBackend::UNKNOWN, GraphicsBackend::FBDEV}) == 0);
    assert(gr_pixel_format() == PixelFormat::RGBX && !gr_has_multiple_connectors());
    gr_color(10, 20, 30, 255);
    gr_clear();
    gr_color(200, 100, 50, 255);
    gr_fill(1, 0, 3, 1); // minui uses bottom/right endpoints, not width/height.
    assert(surface->data()[0] == 10 && surface->data()[4] == 200);
    auto clone = surface->Clone();
    assert(clone && clone->data() != surface->data());
    assert(!memcmp(clone->data(), surface->data(), surface->height * surface->row_bytes));
    assert(gr_save_screenshot(argv[1]) == 0);
    GRSurface* loaded = nullptr;
    assert(res_create_display_surface(argv[1], &loaded) == 0);
    assert(gr_get_width(loaded) == 3 && gr_get_height(loaded) == 2);
    res_free_surface(loaded);
    gr_exit();
    const auto exits = backend_exits;
    gr_exit();
    assert(backend_exits == exits);
    // minui properties take priority; TWRP persisted rotation overrides the default.
    surface = GRSurface::Create(100, 200, 416, 4);
    android::base::properties["ro.minui.default_rotation"] = "ROTATION_LEFT";
    android::base::properties["ro.minui.overscan_percent"] = "10";
    assert(gr_init() == 0 && gr_get_rotation() == GRRotation::LEFT);
    assert(gr_fb_width() == 160 && gr_fb_height() == 80);
    gr_exit();
    android::base::properties["persist.twrp.rotation"] = "90";
    assert(gr_init() == 0 && gr_get_rotation() == GRRotation::RIGHT);
    const uint8_t pixel[] = {50, 100, 200, 255};
    const int expected_x[] = {10, 89, 89, 10};
    const int expected_y[] = {20, 20, 179, 179};
    for (int rotation = 0; rotation < 4; ++rotation) {
        memset(surface->data(), 0, surface->data_size());
        gr_rotate(static_cast<GRRotation>(rotation));
        assert(gr_blit_raw(pixel, 1, 1, 4, 0, 0) == 0);
        const auto* destination = surface->data() + expected_y[rotation] * 416 + expected_x[rotation] * 4;
        assert(destination[0] == 200 && destination[1] == 100 && destination[2] == 50);
        assert(surface->data()[0] == 0);
    }
    gr_exit();
    android::base::properties.clear();
    fail_init = true;
    assert(gr_init({GraphicsBackend::FBDEV}) == -1);
    gr_exit();
    std::remove(argv[1]);
    std::puts("PASS: 7 formats x 4 rotations, padded rows, PNG round trips, flip/damage, native minui API/resources, rotation properties, overscan, failed init/cleanup");
}
