#pragma once
#include <string>
#include <minui/minui.h>

int CreateSurfaceFromSVG(const std::string& svg, int req_w, int req_h, GRSurface** out);