#include "svg_image.h"
#ifdef TW_INCLUDE_SVG
#include <lunasvg.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>

static GRSurface* AllocSurface(int w, int h, int row_bytes) {
    auto* s = static_cast<GRSurface*>(calloc(1, sizeof(GRSurface)));
    if (!s) return nullptr;
    s->width       = w;
    s->height      = h;
    s->row_bytes   = row_bytes;
    s->pixel_bytes = 4; // RGBA8888
    size_t sz      = static_cast<size_t>(row_bytes) * h;
    s->data        = static_cast<unsigned char*>(malloc(sz));
    if (!s->data) { free(s); return nullptr; }
    return s;
}

int CreateSurfaceFromSVG(const std::string& svg, int req_w, int req_h, GRSurface** out) {
    *out = nullptr;

    auto doc = lunasvg::Document::loadFromData(svg);
    if (!doc) return -1;

    // 计算渲染尺寸：优先用传入尺寸，否则用 SVG 自身尺寸/viewBox
    int w = doc->width();
    int h = doc->height();
    if (w <= 0 || h <= 0) {
        auto vb = doc->viewBox();
        w = std::max(1, (int)vb.width());
        h = std::max(1, (int)vb.height());
    }
    if (req_w > 0) w = req_w;
    if (req_h > 0) h = req_h;

    // 渲染到位图（RGBA）
    auto bmp = doc->renderToBitmap(w, h);
    if (!bmp || bmp.width() <= 0 || bmp.height() <= 0) return -2;

    int stride = bmp.stride(); // 字节对齐后的每行字节数
    GRSurface* s = AllocSurface(bmp.width(), bmp.height(), stride);
    if (!s) return -3;

    // 拷贝像素
    size_t sz = static_cast<size_t>(stride) * s->height;
    memcpy(s->data, bmp.data(), sz);

    *out = s;
    return 0;
}
#else
int CreateSurfaceFromSVG(const std::string&, int, int, GRSurface**) { return -99; }
#endif
