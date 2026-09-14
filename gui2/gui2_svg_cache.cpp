#include "gui2_svg_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "src/libs/thorvg/thorvg_capi.h"

namespace {

struct raster_entry {
  const lv_image_dsc_t* source = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
  std::vector<uint8_t> pixels;
  lv_image_dsc_t image{};
};

std::vector<std::unique_ptr<raster_entry>> cache;

bool get_raster_size(const lv_image_dsc_t* source, int target_width, int target_height,
                     uint16_t* width, uint16_t* height) {
  if (source == nullptr || source->data == nullptr || source->data_size == 0 ||
      source->header.w == 0 || source->header.h == 0 || width == nullptr || height == nullptr)
    return false;

  const int safe_target_width = std::max(1, target_width);
  const int safe_target_height = std::max(1, target_height);
  const double scale = std::min(static_cast<double>(safe_target_width) / source->header.w,
                                static_cast<double>(safe_target_height) / source->header.h);

  // Do not downsample below the SVG's declared size.  For GUI2's static
  // icons this also makes the raster the same size as its display box, so
  // LVGL does not need to scale it again during normal drawing.
  const double final_scale = std::max(1.0, scale);
  const int raster_width =
      std::max(1, static_cast<int>(std::lround(source->header.w * final_scale)));
  const int raster_height =
      std::max(1, static_cast<int>(std::lround(source->header.h * final_scale)));
  if (raster_width > UINT16_MAX || raster_height > UINT16_MAX) return false;

  *width = static_cast<uint16_t>(raster_width);
  *height = static_cast<uint16_t>(raster_height);
  return true;
}

raster_entry* rasterize(const lv_image_dsc_t* source, uint16_t width, uint16_t height) {
  auto entry = std::make_unique<raster_entry>();
  entry->source = source;
  entry->width = width;
  entry->height = height;
  entry->pixels.resize(static_cast<size_t>(width) * height * sizeof(uint32_t), 0);

  Tvg_Paint* picture = tvg_picture_new();
  Tvg_Canvas* canvas = nullptr;
  if (picture == nullptr ||
      tvg_picture_load_data(picture, reinterpret_cast<const char*>(source->data), source->data_size,
                            "svg", true) != TVG_RESULT_SUCCESS ||
      tvg_picture_set_size(picture, width, height) != TVG_RESULT_SUCCESS) {
    if (picture != nullptr) tvg_paint_del(picture);
    return nullptr;
  }

  canvas = tvg_swcanvas_create();
  if (canvas == nullptr ||
      tvg_swcanvas_set_target(canvas, reinterpret_cast<uint32_t*>(entry->pixels.data()), width,
                              width, height, TVG_COLORSPACE_ARGB8888) != TVG_RESULT_SUCCESS ||
      tvg_canvas_push(canvas, picture) != TVG_RESULT_SUCCESS ||
      tvg_canvas_draw(canvas) != TVG_RESULT_SUCCESS ||
      tvg_canvas_sync(canvas) != TVG_RESULT_SUCCESS) {
    // A picture pushed to a canvas is owned by that canvas.  If it was not
    // pushed, release it here; otherwise canvas destruction releases it.
    if (canvas == nullptr) {
      tvg_paint_del(picture);
    } else {
      tvg_canvas_destroy(canvas);
    }
    return nullptr;
  }
  tvg_canvas_destroy(canvas);

  entry->image.header.magic = LV_IMAGE_HEADER_MAGIC;
  entry->image.header.cf = LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED;
  entry->image.header.flags = LV_IMAGE_FLAGS_PREMULTIPLIED;
  entry->image.header.w = width;
  entry->image.header.h = height;
  entry->image.header.stride = static_cast<uint32_t>(width) * sizeof(uint32_t);
  entry->image.header.reserved_2 = 0;
  entry->image.data_size = static_cast<uint32_t>(entry->pixels.size());
  entry->image.data = entry->pixels.data();
  entry->image.reserved = nullptr;
  entry->image.reserved_2 = nullptr;

  raster_entry* result = entry.get();
  cache.emplace_back(std::move(entry));
  return result;
}

}  // namespace

const lv_image_dsc_t* gui2_svg_get_raster(const lv_image_dsc_t* svg, int target_width,
                                          int target_height) {
  uint16_t width = 0;
  uint16_t height = 0;
  if (!get_raster_size(svg, target_width, target_height, &width, &height)) {
    fprintf(stderr, "gui2: invalid SVG asset\n");
    return svg;
  }

  for (const auto& item : cache) {
    if (item->source == svg && item->width == width && item->height == height) return &item->image;
  }

  raster_entry* item = rasterize(svg, width, height);
  if (item == nullptr) {
    fprintf(stderr, "gui2: failed to rasterize SVG asset\n");
    return svg;
  }
  return &item->image;
}

void gui2_svg_cache_clear() {
  cache.clear();
}
