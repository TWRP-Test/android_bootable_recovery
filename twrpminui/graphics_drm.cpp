/*
 * Copyright (C) 2015 The Android Open Source Project
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

// Baseline: current minui/graphics_drm.cpp; local differences are marked TWRP.
// Historical hand-ports 673bffeb (atomic), bee78dde (topology) and edf59d8b
// (SPR) are superseded by minui's CLO implementations, including reverse pipe
// allocation, secondary connectors and demura handoff. TWRP fixes are layered below.

/*
 * DRM based mode setting test program
 * Copyright 2008 Tungsten Graphics
 *   Jakob Bornecrantz <jakob@tungstengraphics.com>
 * Copyright 2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "graphics_drm.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
// TWRP: raw frame synchronization and optional presentation statistics.
#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <limits>

#include <android-base/macros.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <string>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sstream>

#include "twrpminui/minui.h"

#define find_prop_id(_res, type, Type, obj_id, prop_name, prop_id, index)            \
  do {                                                                               \
    int j = 0;                                                                       \
    int prop_count = 0;                                                              \
    struct Type* obj = NULL;                                                         \
    obj = (_res);                                                                    \
    if (!obj || !(obj->props) || drm[index].monitor_##type->type##_id != (obj_id)) { \
      prop_id = 0;                                                                   \
      break;                                                                         \
    }                                                                                \
    prop_count = (int)obj->props->count_props;                                       \
    for (j = 0; j < prop_count; ++j)                                                 \
      if (!strcmp(obj->props_info[j]->name, (prop_name))) break;                     \
    (prop_id) = (j == prop_count) ? 0 : obj->props_info[j]->prop_id;                 \
  } while (0)

#define add_prop(res, type, Type, id, id_name, id_val, index) \
  find_prop_id(res, type, Type, id, id_name, prop_id, index); \
  if (prop_id)                                         \
    drmModeAtomicAddProperty(atomic_req, id, prop_id, id_val);

/**
 * enum sde_rm_topology_name - HW resource use case in use by connector
 * @SDE_RM_TOPOLOGY_NONE:                 No topology in use currently
 * @SDE_RM_TOPOLOGY_SINGLEPIPE:           1 LM, 1 PP, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_SINGLEPIPE_DSC:       1 LM, 1 DSC, 1 PP, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_SINGLEPIPE_VDC:       1 LM, 1 VDC, 1 PP, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE:             2 LM, 2 PP, 2 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_DSC:         2 LM, 2 DSC, 2 PP, 2 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE:     2 LM, 2 PP, 3DMux, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_DSC: 2 LM, 2 PP, 3DMux, 1 DSC, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_VDC: 2 LM, 2 PP, 3DMux, 1 VDC, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_DSCMERGE:    2 LM, 2 PP, 2 DSC Merge, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_PPSPLIT:              1 LM, 2 PPs, 2 INTF/WB
 * @SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE      4 LM, 4 PP, 3DMux, 2 INTF
 * @SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE_DSC  4 LM, 4 PP, 3DMux, 3 DSC, 2 INTF
 * @SDE_RM_TOPOLOGY_QUADPIPE_DSCMERE      4 LM, 4 PP, 4 DSC Merge, 2 INTF
 * @SDE_RM_TOPOLOGY_QUADPIPE_DSC4HSMERGE  4 LM, 4 PP, 4 DSC Merge, 1 INTF
 */

static uint32_t get_lm_number(const std::string &topology) {
  if (topology == "sde_singlepipe") return 1;
  if (topology == "sde_singlepipe_dsc") return 1;
  if (topology == "sde_singlepipe_vdc") return 1;
  if (topology == "sde_dualpipe") return 2;
  if (topology == "sde_dualpipe_dsc") return 2;
  if (topology == "sde_dualpipe_vdc") return 2;
  if (topology == "sde_dualpipemerge") return 2;
  if (topology == "sde_dualpipemerge_dsc") return 2;
  if (topology == "sde_dualpipemerge_vdc") return 2;
  if (topology == "sde_dualpipe_dscmerge") return 2;
  if (topology == "sde_ppsplit") return 1;
  if (topology == "sde_quadpipemerge") return 4;
  if (topology == "sde_quadpipe_3dmerge_dsc") return 4;
  if (topology == "sde_quadpipe_dscmerge") return 4;
  if (topology == "sde_quadpipe_dsc4hsmerge") return 4;
  return DEFAULT_NUM_LMS;
}

static uint32_t get_topology_lm_number(int fd, uint32_t blob_id) {
  uint32_t num_lm = DEFAULT_NUM_LMS;

  drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    return num_lm;
  }

  const char *fmt_str = (const char *)(blob->data);
  std::stringstream stream(fmt_str);
  std::string line = {};
  const std::string topology = "topology=";

  while (std::getline(stream, line)) {
    if (line.find(topology) != std::string::npos) {
        num_lm = get_lm_number(std::string(line, topology.length()));
        break;
    }
  }

  drmModeFreePropertyBlob(blob);
  return num_lm;
}

static bool is_primary_plane(int fd, uint32_t blob_id) {

  bool primary = false;
  uint32_t master_plane_id_val = 0;
  drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(fd, blob_id);
  if (!blob)
    return false;

  const char *fmt_str = (const char *)(blob->data);
  std::stringstream stream(fmt_str);
  std::string line = {};
  const std::string master_plane = "primary_smart_plane_id=";

  while (std::getline(stream, line)) {
    if (line.find(master_plane) != std::string::npos) {
        line = std::string(line, master_plane.length());
        master_plane_id_val = std::atoi(line.c_str());
        break;
    }
  }

  drmModeFreePropertyBlob(blob);
  primary = master_plane_id_val ? false : true;
  return primary;
}

static int find_plane_prop_id(uint32_t obj_id, const char *prop_name,
                              Plane *plane_res) {
  int i, j = 0;
  int prop_count = 0;
  struct Plane *obj = NULL;

  for (i = 0; i < NUM_PLANES; ++i) {
    obj = &plane_res[i];
    if (!obj->plane || !obj->props || obj->plane->plane_id != obj_id) // TWRP: unused pipe slots.
      continue;
    prop_count = (int)obj->props->count_props;
    for (j = 0; j < prop_count; ++j)
      if (!strcmp(obj->props_info[j]->name, prop_name))
       return obj->props_info[j]->prop_id;
    break;
  }

  return 0;
}

static int atomic_add_prop_to_plane(Plane *plane_res, drmModeAtomicReq *req,
                                    uint32_t obj_id, const char *prop_name,
                                    uint64_t value) {
  uint32_t prop_id;

  prop_id = find_plane_prop_id(obj_id, prop_name, plane_res);
  if (prop_id == 0) {
    printf("Could not find obj_id = %d\n", obj_id);
    return -EINVAL;
  }

  if (drmModeAtomicAddProperty(req, obj_id, prop_id, value) < 0) {
    printf("Could not add prop_id = %d for obj_id %d\n",
            prop_id, obj_id);
    return -EINVAL;
  }

  return 0;
}

static int SetupSprBlobV1(int fd, uint32_t* blob_id) {
  SPRPackType pack_type = SPRPackType::kPentile;
  SPRFilterType filter_type = SPRFilterType::kFourTap;
  SPRAdaptiveModeType adpative_mode = SPRAdaptiveModeType::kYYGM;

  drm_msm_spr_init_cfg spr_init_cfg{}; // TWRP: initialize every kernel ABI field (60acba5e).
  // TWRP: panel-requested bypass (6ee9a21b).
  spr_init_cfg.flags = android::base::GetBoolProperty("vendor.display.enable_spr_bypass", false) ? 1 : 0;
  spr_init_cfg.cfg0 = 1;
  spr_init_cfg.cfg1 = 1;
  spr_init_cfg.cfg2 = 1;
  spr_init_cfg.cfg3 = 0;
  spr_init_cfg.cfg4 = (pack_type == SPRPackType::kRGBW);
  spr_init_cfg.cfg5 = kDefaultColorPhaseIncrement.at(pack_type);
  spr_init_cfg.cfg6 = kDefaultColorPhaseRepeat.at(pack_type);
  spr_init_cfg.cfg7 = static_cast<uint16_t>(filter_type);
  spr_init_cfg.cfg8 = static_cast<uint16_t>(adpative_mode);
  if (pack_type == SPRPackType::kRGBW) {
    spr_init_cfg.cfg9 = 512;
    std::copy(kDefaultRGBWGains.begin(), kDefaultRGBWGains.end(), spr_init_cfg.cfg12); // TWRP: gains live in cfg12 (60acba5e).
  }
  spr_init_cfg.cfg10 = 0;
  std::copy(kDecimationRatioMap.at(pack_type).begin(), kDecimationRatioMap.at(pack_type).end(),
            spr_init_cfg.cfg11);
  std::copy(kDefaultOPRGains.begin(), kDefaultOPRGains.end(), spr_init_cfg.cfg13);
  std::copy(kDefaultAdaptiveStrengths.begin(), kDefaultAdaptiveStrengths.end(), spr_init_cfg.cfg14);
  std::copy(kDefaultOPROffsets.begin(), kDefaultOPROffsets.end(), spr_init_cfg.cfg15);
  std::copy(kDefaultFilterCoeffsMap.at(filter_type).begin(),
            kDefaultFilterCoeffsMap.at(filter_type).end(), spr_init_cfg.cfg16);
  std::copy(kDefaultColorPhaseMap.at(pack_type).begin(), kDefaultColorPhaseMap.at(pack_type).end(),
            spr_init_cfg.cfg17);

  if (drmModeCreatePropertyBlob(fd, &spr_init_cfg, sizeof(drm_msm_spr_init_cfg), blob_id)) {
    printf("failed to create spr blob\n");
    return -EINVAL;
  }

  return 0;
}

static int SetupSprBlobV2(int fd, uint32_t* blob_id) {
  SPRPackType pack_type = SPRPackType::kPentile;
  SPRFilterType filter_type = SPRFilterType::kFourTap;
  SPRAdaptiveModeType adpative_mode = SPRAdaptiveModeType::kYYGM;

  drm_msm_spr_init_cfg_v2 spr_init_cfg_v2{}; // TWRP: 60acba5e.
  // TWRP: panel-requested bypass (6ee9a21b).
  spr_init_cfg_v2.flags = android::base::GetBoolProperty("vendor.display.enable_spr_bypass", false) ? 1 : 0;
  spr_init_cfg_v2.cfg0 = 1;
  spr_init_cfg_v2.cfg1 = 1;
  spr_init_cfg_v2.cfg2 = 1;
  spr_init_cfg_v2.cfg3 = 0;
  spr_init_cfg_v2.cfg4 = (pack_type == SPRPackType::kRGBW);
  spr_init_cfg_v2.cfg5 = kDefaultColorPhaseIncrement.at(pack_type);
  spr_init_cfg_v2.cfg6 = kDefaultColorPhaseRepeat.at(pack_type);
  spr_init_cfg_v2.cfg7 = static_cast<uint16_t>(filter_type);
  spr_init_cfg_v2.cfg8 = static_cast<uint16_t>(adpative_mode);
  if (pack_type == SPRPackType::kRGBW) {
    spr_init_cfg_v2.cfg9 = 512;
    std::copy(kDefaultRGBWGains.begin(), kDefaultRGBWGains.end(), spr_init_cfg_v2.cfg12); // TWRP: 60acba5e.
  }
  spr_init_cfg_v2.cfg10 = 0;
  std::copy(kDecimationRatioMap.at(pack_type).begin(), kDecimationRatioMap.at(pack_type).end(),
            spr_init_cfg_v2.cfg11);
  std::copy(kDefaultOPRGains.begin(), kDefaultOPRGains.end(), spr_init_cfg_v2.cfg13);
  std::copy(kDefaultAdaptiveStrengths.begin(), kDefaultAdaptiveStrengths.end(),
            spr_init_cfg_v2.cfg14);
  std::copy(kDefaultOPROffsets.begin(), kDefaultOPROffsets.end(), spr_init_cfg_v2.cfg15);
  std::copy(kDefaultFilterCoeffsMap.at(filter_type).begin(),
            kDefaultFilterCoeffsMap.at(filter_type).end(), spr_init_cfg_v2.cfg16);
  std::copy(kDefaultColorPhaseMap.at(pack_type).begin(), kDefaultColorPhaseMap.at(pack_type).end(),
            spr_init_cfg_v2.cfg17);

  if (drmModeCreatePropertyBlob(fd, &spr_init_cfg_v2, sizeof(drm_msm_spr_init_cfg_v2), blob_id)) {
    printf("failed to create spr blob\n");
    return -EINVAL;
  }

  return 0;
}

static int SetupSprBlob(int fd, const std::string& prop_name, uint32_t* blob_id) {
  int ret = 0;
  if (prop_name == "SDE_SPR_INIT_CFG_V1") {
    ret = SetupSprBlobV1(fd, blob_id);
  } else if (prop_name == "SDE_SPR_INIT_CFG_V2") {
    ret = SetupSprBlobV2(fd, blob_id);
  } else {
    ret = -ENOENT;
  }

  return ret;
}

int MinuiBackendDrm::AtomicPopulatePlane(int plane, drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  // TWRP: isolate CLO pipeline state between main and secondary connectors.
  auto& plane_res = drm[index].plane_res;
  auto& number_of_lms = drm[index].number_of_lms;

  uint32_t src_x, src_y, src_w, src_h;
  uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
  int width = drm[index].monitor_crtc->mode.hdisplay;
  int height = drm[index].monitor_crtc->mode.vdisplay;
  int zpos = 0;

  src_y = 0;
  src_w = width/number_of_lms;
  src_h =  height;
  crtc_y = 0;
  crtc_w = width/number_of_lms;
  crtc_h = height;
  src_x = (width/number_of_lms) * plane;
  crtc_x = (width/number_of_lms) * plane;

  /* populate z-order property required for 4 layer mixer */
  if (number_of_lms == 4)
    zpos = plane >> 1;

  atomic_add_prop_to_plane(plane_res, atomic_req,
                           plane_res[plane].plane->plane_id, "zpos", zpos);

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "FB_ID",
                               drm[index].GRSurfaceDrms[drm[index].current_buffer]->fb_id))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_X", src_x << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_Y", src_y << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_W", src_w << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_H", src_h << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_X", crtc_x))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_Y", crtc_y))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_W", crtc_w))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_H", crtc_h))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_ID",
                               drm[index].monitor_crtc->crtc_id))
    return -EINVAL;

  return 0;
}

int MinuiBackendDrm::TeardownPipeline(drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  // TWRP: isolate CLO pipeline state between main and secondary connectors.
  auto& crtc_res = drm[index].crtc_res;
  auto& conn_res = drm[index].conn_res;
  auto& plane_res = drm[index].plane_res;
  auto& number_of_lms = drm[index].number_of_lms;
  auto& fb_prop_id = drm[index].fb_prop_id;
  auto& spr_enabled = drm[index].spr_enabled;
  auto& spr_prop_name = drm[index].spr_prop_name;

  uint32_t i, prop_id;
  int ret;

  /* During suspend, tear down pipeline */
  add_prop(&conn_res, connector, Connector, drm[index].monitor_connector->connector_id, "CRTC_ID", 0, index);
  add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "MODE_ID", 0, index);
  add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "ACTIVE", 0, index);
  if (spr_enabled) {
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, spr_prop_name.c_str(), 0,
             index);
  }

  for(i = 0; i < number_of_lms; i++) {
    ret = atomic_add_prop_to_plane(plane_res, atomic_req,
                                   plane_res[i].plane->plane_id, "CRTC_ID", 0);
    if (ret < 0) {
      printf("Failed to tear down plane %d\n", i);
      return ret;
    }

    if (drmModeAtomicAddProperty(atomic_req, plane_res[i].plane->plane_id, fb_prop_id, 0) < 0) {
      printf("Failed to add property for plane_id=%d\n", plane_res[i].plane->plane_id);
      return -EINVAL;
    }
  }

  return 0;
}

int MinuiBackendDrm::SetupPipeline(drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  // TWRP: isolate CLO pipeline state between main and secondary connectors.
  auto& crtc_res = drm[index].crtc_res;
  auto& conn_res = drm[index].conn_res;
  auto& plane_res = drm[index].plane_res;
  auto& number_of_lms = drm[index].number_of_lms;
  auto& spr_enabled = drm[index].spr_enabled;
  auto& spr_prop_name = drm[index].spr_prop_name;
  auto& demura_prop_name = drm[index].demura_prop_name;

  uint32_t i, prop_id;
  int ret;

  for(i = 0; i < number_of_lms; i++) {
    add_prop(&conn_res, connector, Connector, drm[index].monitor_connector->connector_id,
         "CRTC_ID", drm[index].monitor_crtc->crtc_id, index);
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "MODE_ID", crtc_res.mode_blob_id, index);
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "ACTIVE", 1, index);
    if (spr_enabled) {
      add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, spr_prop_name.c_str(),
               crtc_res.spr_blob_id, index);
    }
    /* Disable demura for hand off */
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, demura_prop_name.c_str(), 0,
             index);
  }

  /* Setup planes */
  for(i = 0; i < number_of_lms; i++) {
    ret = AtomicPopulatePlane(i, atomic_req, index);
    if (ret < 0) {
      printf("Error populating plane_id = %d\n", plane_res[i].plane->plane_id);
      return ret;
    }
  }

  return 0;
}

GRSurfaceDrm::~GRSurfaceDrm() {
  if (mmapped_buffer_) {
    munmap(mmapped_buffer_, row_bytes * height);
  }

  if (fb_id) {
    if (drmModeRmFB(drm_fd_, fb_id) != 0) {
      perror("Failed to drmModeRmFB");
      // Falling through to free other resources.
    }
  }

  if (handle) {
    drm_gem_close gem_close = {};
    gem_close.handle = handle;

    if (drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close) != 0) {
      perror("Failed to DRM_IOCTL_GEM_CLOSE");
    }
  }
}

static int drm_format_to_bpp(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
      return 32;
    case DRM_FORMAT_RGB565:
      return 16;
    default:
      printf("Unknown format %d\n", format);
      return 32;
  }
}

std::unique_ptr<GRSurfaceDrm> GRSurfaceDrm::Create(int drm_fd, int width, int height) {
  uint32_t format;
  PixelFormat pixel_format = gr_pixel_format();
  // PixelFormat comes in byte order, whereas DRM_FORMAT_* uses little-endian
  // (external/libdrm/include/drm/drm_fourcc.h). Note that although drm_fourcc.h also defines a
  // macro of DRM_FORMAT_BIG_ENDIAN, it doesn't seem to be actually supported (see the discussion
  // in https://lists.freedesktop.org/archives/amd-gfx/2017-May/008560.html).
  if (pixel_format == PixelFormat::ABGR) {
    format = DRM_FORMAT_RGBA8888;
  } else if (pixel_format == PixelFormat::BGRA) {
    format = DRM_FORMAT_ARGB8888;
  } else if (pixel_format == PixelFormat::RGBX) {
    format = DRM_FORMAT_XBGR8888;
  } else if (pixel_format == PixelFormat::ARGB) {
    format = DRM_FORMAT_BGRA8888;
  } else {
    format = DRM_FORMAT_RGB565;
  }

  // TWRP: native byte-order formats include RGBA/RGB565 (7756859a, c52a090f).
  switch (gr_frame_pixel_format()) {
    case GRPixelFormat::RGBA8888: format = DRM_FORMAT_ABGR8888; break;
    case GRPixelFormat::RGBX8888: format = DRM_FORMAT_XBGR8888; break;
    case GRPixelFormat::BGRA8888: format = DRM_FORMAT_ARGB8888; break;
    case GRPixelFormat::ABGR8888: format = DRM_FORMAT_RGBA8888; break;
    case GRPixelFormat::ARGB8888:
    case GRPixelFormat::XRGB8888: format = DRM_FORMAT_BGRA8888; break;
    case GRPixelFormat::RGB565: format = DRM_FORMAT_RGB565; break;
    default: return nullptr;
  }

  drm_mode_create_dumb create_dumb = {};
  create_dumb.height = height;
  create_dumb.width = width;
  create_dumb.bpp = drm_format_to_bpp(format);
  create_dumb.flags = 0;

  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) != 0) {
    perror("Failed to DRM_IOCTL_MODE_CREATE_DUMB");
    return nullptr;
  }
  printf("Allocating buffer with resolution %d x %d pitch: %d bpp: %d, size: %llu\n", width, height,
         create_dumb.pitch, create_dumb.bpp, create_dumb.size);

  // Cannot use std::make_unique to access non-public ctor.
  auto surface = std::unique_ptr<GRSurfaceDrm>(new GRSurfaceDrm(
      width, height, create_dumb.pitch, create_dumb.bpp / 8, drm_fd, create_dumb.handle));

  uint32_t handles[4]{}, pitches[4]{}, offsets[4]{}; // TWRP: no uninitialized plane metadata.

  handles[0] = surface->handle;
  pitches[0] = create_dumb.pitch;
  offsets[0] = 0;
  if (drmModeAddFB2(drm_fd, width, height, format, handles, pitches, offsets, &surface->fb_id, 0) !=
      0) {
    perror("Failed to drmModeAddFB2");
    return nullptr;
  }

  drm_mode_map_dumb map_dumb = {};
  map_dumb.handle = create_dumb.handle;
  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) != 0) {
    perror("Failed to DRM_IOCTL_MODE_MAP_DUMB");
    return nullptr;
  }

  auto mmapped =
      mmap(nullptr, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, map_dumb.offset);
  if (mmapped == MAP_FAILED) {
    perror("Failed to mmap()");
    return nullptr;
  }
  surface->mmapped_buffer_ = static_cast<uint8_t*>(mmapped);
  // TWRP: initial scanout must contain a complete, defined frame.
  memset(surface->mmapped_buffer_, 0, create_dumb.size);
  printf("Framebuffer of size %llu allocated @ %p\n", create_dumb.size, surface->mmapped_buffer_);
  return surface;
}

int MinuiBackendDrm::DrmDisableCrtc(drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  return TeardownPipeline(atomic_req, index);
}

int MinuiBackendDrm::DrmEnableCrtc(drmModeAtomicReqPtr atomic_req, DrmConnector index){
  return SetupPipeline(atomic_req, index);
}

void MinuiBackendDrm::Blank(bool blank) {
  Blank(blank, DRM_MAIN);
}

#if 0 // TWRP: unmodified minui reference; retained for future resyncs.
int MinuiBackendDrm::Initdisplay(DrmConnector index) {
  /* Get possible plane_ids */
  drmModePlaneRes* plane_options = drmModeGetPlaneResources(drm_fd);
  if (!plane_options || !plane_options->planes || (plane_options->count_planes < number_of_lms))
    return -1;

  /* Set crtc resources */
  crtc_res.props =
      drmModeObjectGetProperties(drm_fd, drm[index].monitor_crtc->crtc_id, DRM_MODE_OBJECT_CRTC);
  if (!crtc_res.props) return -1;

  crtc_res.props_info = static_cast<drmModePropertyRes**>(
      calloc(crtc_res.props->count_props, sizeof(crtc_res.props_info)));
  if (!crtc_res.props_info)
    return -1;
  else
    for (int j = 0; j < (int)crtc_res.props->count_props; ++j) {
      crtc_res.props_info[j] = drmModeGetProperty(drm_fd, crtc_res.props->props[j]);
      /* Get spr property name */
      if (!strcmp(crtc_res.props_info[j]->name, "SDE_SPR_INIT_CFG_V1") ||
          !strcmp(crtc_res.props_info[j]->name, "SDE_SPR_INIT_CFG_V2")) {
        spr_prop_name = crtc_res.props_info[j]->name;
      }
      /* Get demura property name */
      if (!strcmp(crtc_res.props_info[j]->name, "SDE_DEMURA_INIT_CFG_V1") ||
          !strcmp(crtc_res.props_info[j]->name, "SDE_DEMURA_INIT_CFG_V3")) {
        demura_prop_name = crtc_res.props_info[j]->name;
      }
    }

  /* Set connector resources */
  conn_res.props = drmModeObjectGetProperties(drm_fd, drm[index].monitor_connector->connector_id,
                                              DRM_MODE_OBJECT_CONNECTOR);
  if (!conn_res.props) return -1;

  conn_res.props_info = static_cast<drmModePropertyRes**>(
      calloc(conn_res.props->count_props, sizeof(conn_res.props_info)));
  if (!conn_res.props_info)
    return -1;
  else {
    for (int j = 0; j < (int)conn_res.props->count_props; ++j) {
      conn_res.props_info[j] = drmModeGetProperty(drm_fd, conn_res.props->props[j]);

      /* Get preferred mode information and extract the
       * number of layer mixers needed from the topology name.
       */
      if (!strcmp(conn_res.props_info[j]->name, "mode_properties")) {
        number_of_lms = get_topology_lm_number(drm_fd, conn_res.props->prop_values[j]);
        printf("number of lms in topology %d\n", number_of_lms);
      }
    }
  }

  printf("plane count:%d\n",plane_options->count_planes);
  struct Plane *plane_all_res;
  uint32_t counter = 0;
  plane_all_res = static_cast<struct Plane *>(
     calloc(plane_options->count_planes, sizeof(struct Plane)));
  /* Set plane resources */
  for (uint32_t i = 0; i < plane_options->count_planes; ++i) {
    plane_all_res[i].plane = drmModeGetPlane(drm_fd, plane_options->planes[i]);
    if (!plane_all_res[i].plane)return -1;
  }

  for (uint32_t i = plane_options->count_planes - 1; i >= 0; i--) {
    struct Plane* obj = &plane_all_res[i];
    unsigned int j;
    obj->props = drmModeObjectGetProperties(drm_fd, obj->plane->plane_id, DRM_MODE_OBJECT_PLANE);
    if (!obj->props) continue;
    obj->props_info = static_cast<drmModePropertyRes**>(
        calloc(obj->props->count_props, sizeof(*obj->props_info)));
    if (!obj->props_info) continue;
    for (j = 0; j < obj->props->count_props; ++j) {
      obj->props_info[j] = drmModeGetProperty(drm_fd, obj->props->props[j]);

      if (!strcmp(obj->props_info[j]->name, "capabilities")) {
        if(is_primary_plane(drm_fd, obj->props->prop_values[j])) {
          plane_res[counter] = plane_all_res[i];
          counter++;
      }
     }
   }
   if (counter == number_of_lms)
     break;
  }

  drmModeFreePlaneResources(plane_options);
  plane_options = NULL;

  /* Setup spr blob id if enabled */
  if (spr_enabled) {
    if (SetupSprBlob(drm_fd, spr_prop_name, &crtc_res.spr_blob_id)) {
      return -1;
    }
  }

  /* Setup pipe and blob_id */
  if (drmModeCreatePropertyBlob(drm_fd, &drm[index].monitor_crtc->mode, sizeof(drmModeModeInfo),
                                &crtc_res.mode_blob_id)) {
    printf("failed to create mode blob\n");
    return -1;
  }

  /* Save fb_prop_id*/
  uint32_t prop_id;
  prop_id = find_plane_prop_id(plane_res[0].plane->plane_id, "FB_ID", plane_res);
  fb_prop_id = prop_id;
  return 0;
}
#else // TWRP: active implementation below preserves GUI2/device requirements.
int MinuiBackendDrm::Initdisplay(DrmConnector index) {

  // TWRP: isolate CLO pipeline state between main and secondary connectors.
  auto& crtc_res = drm[index].crtc_res;
  auto& conn_res = drm[index].conn_res;
  auto& plane_res = drm[index].plane_res;
  auto& number_of_lms = drm[index].number_of_lms;
  auto& fb_prop_id = drm[index].fb_prop_id;
  auto& spr_enabled = drm[index].spr_enabled;
  auto& spr_prop_name = drm[index].spr_prop_name;
  auto& demura_prop_name = drm[index].demura_prop_name;

  // TWRP: cache metadata across blank/unblank, release it with the owning fd.
  if (drm[index].pipeline_ready) return 0;
  if (crtc_res.props || conn_res.props) ReleasePipeline(index);
  auto load_properties = [&](uint32_t id, uint32_t type, auto* object) {
    object->props = drmModeObjectGetProperties(drm_fd, id, type);
    if (!object->props) return false;
    object->props_info = static_cast<drmModePropertyRes**>(
        calloc(object->props->count_props, sizeof(*object->props_info)));
    if (!object->props_info && object->props->count_props) return false;
    for (uint32_t i = 0; i < object->props->count_props; ++i) {
      object->props_info[i] = drmModeGetProperty(drm_fd, object->props->props[i]);
      if (!object->props_info[i]) return false;
    }
    return true;
  };
  if (!load_properties(drm[index].monitor_crtc->crtc_id, DRM_MODE_OBJECT_CRTC, &crtc_res) ||
      !load_properties(drm[index].monitor_connector->connector_id, DRM_MODE_OBJECT_CONNECTOR,
                       &conn_res)) return -1;
  for (uint32_t i = 0; i < crtc_res.props->count_props; ++i) {
    const std::string name = crtc_res.props_info[i]->name;
    if (name == "SDE_SPR_INIT_CFG_V1" || name == "SDE_SPR_INIT_CFG_V2") spr_prop_name = name;
    if (name == "SDE_DEMURA_INIT_CFG_V1" || name == "SDE_DEMURA_INIT_CFG_V3") demura_prop_name = name;
  }
  // CLO: extract topology and select pipes in reverse population order.
  for (uint32_t i = 0; i < conn_res.props->count_props; ++i) {
    if (!strcmp(conn_res.props_info[i]->name, "mode_properties"))
      number_of_lms = get_topology_lm_number(drm_fd, conn_res.props->prop_values[i]);
  }
  if (number_of_lms == 0 || number_of_lms > NUM_PLANES) {
    fprintf(stderr, "TWRP DRM: invalid mixer count %u on display %d\n", number_of_lms, index);
    return -1;
  }
  std::unique_ptr<drmModePlaneRes, decltype(&drmModeFreePlaneResources)> options(
      drmModeGetPlaneResources(drm_fd), drmModeFreePlaneResources);
  if (!options) return -1;
  uint32_t selected = 0;
  // TWRP: signed reverse traversal avoids CLO's unsigned underflow on missing pipes.
  for (int i = static_cast<int>(options->count_planes) - 1; i >= 0 && selected < number_of_lms; --i) {
    Plane candidate{};
    candidate.plane = drmModeGetPlane(drm_fd, options->planes[i]);
    if (!candidate.plane) continue;
    if (!(candidate.plane->possible_crtcs & (1U << drm[index].crtc_index))) {
      drmModeFreePlane(candidate.plane);
      continue;
    } // TWRP: do not attach another connector's pipes.
    bool primary = false;
    if (load_properties(candidate.plane->plane_id, DRM_MODE_OBJECT_PLANE, &candidate)) {
      for (uint32_t j = 0; j < candidate.props->count_props; ++j) {
        if (!strcmp(candidate.props_info[j]->name, "capabilities"))
          primary = is_primary_plane(drm_fd, candidate.props->prop_values[j]);
      }
    }
    if (primary) {
      plane_res[selected++] = candidate;
    } else {
      if (candidate.props_info) {
        for (uint32_t j = 0; j < candidate.props->count_props; ++j)
          drmModeFreeProperty(candidate.props_info[j]);
        free(candidate.props_info);
      }
      if (candidate.props) drmModeFreeObjectProperties(candidate.props);
      drmModeFreePlane(candidate.plane);
    }
  }
  if (selected != number_of_lms) {
    fprintf(stderr, "TWRP DRM: display %d found %u/%u primary planes for CRTC index %d\n",
            index, selected, number_of_lms, drm[index].crtc_index);
    return -1;
  }
  // TWRP: missing optional SPR property must not disable the whole backend (9388d9af).
  if (spr_enabled && spr_prop_name.empty()) spr_enabled = 0;
  if (spr_enabled && SetupSprBlob(drm_fd, spr_prop_name, &crtc_res.spr_blob_id)) return -1;
  if (drmModeCreatePropertyBlob(drm_fd, &drm[index].monitor_crtc->mode,
                              sizeof(drmModeModeInfo), &crtc_res.mode_blob_id)) return -1;
  if (drm[index].fast_mode.vrefresh &&
      drmModeCreatePropertyBlob(drm_fd, &drm[index].fast_mode,
                                sizeof(drmModeModeInfo), &drm[index].fast_mode_blob_id))
    drm[index].fast_mode_blob_id = 0;
  fb_prop_id = find_plane_prop_id(plane_res[0].plane->plane_id, "FB_ID", plane_res);
  drm[index].pipeline_ready = fb_prop_id != 0;
  return drm[index].pipeline_ready ? 0 : -1;
}
#endif // TWRP


#if 0 // TWRP: unmodified minui reference; retained for future resyncs.
void MinuiBackendDrm::Blank(bool blank, DrmConnector index) {
  const auto* drmInterface = &drm[DRM_MAIN];

  printf("[ENTRY]MinuiBackendDrm::Blank display %d blank %d\n", index, blank);

  switch (index) {
    case DRM_MAIN:
      drmInterface = &drm[DRM_MAIN];
      break;
    case DRM_SEC:
      drmInterface = &drm[DRM_SEC];
      break;
    default:
      fprintf(stderr, "Invalid index: %d\n", index);
      return;
  }

  if (!drmInterface->monitor_connector) {
    fprintf(stderr, "Unsupported. index = %d\n", index);
    return;
  }

  int ret = 0;

  if (blank == current_blank_state[index])
    return;

  drmModeAtomicReqPtr atomic_req = drmModeAtomicAlloc();
  if (!atomic_req) {
     printf("Atomic Alloc failed\n");
     return;
  }

  if (blank)
    ret = DrmDisableCrtc(atomic_req, index);
  else {
    if (Initdisplay(index))
      printf("Failed to init display [%d]\n", index);
    ret = DrmEnableCrtc(atomic_req, index);
    active_display = index;
  }
  printf("[MID]MinuiBackendDrm::DrmEnableCrtc/DrmDisableCrtc[%d] err [%d]\n", blank, ret);

  if (!ret)
    ret = drmModeAtomicCommit(drm_fd, atomic_req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

  if (!ret) {
    printf("Atomic Commit succeed");
    current_blank_state[index] = blank;
  }
  else {
    printf("Atomic Commit failed, rc = %d\n", ret);
  }
  printf("[EXIT]MinuiBackendDrm::Blank display %d blank %d\n", index, blank);

  drmModeAtomicFree(atomic_req);
}
#else // TWRP: active implementation below preserves GUI2/device requirements.
void MinuiBackendDrm::Blank(bool blank, DrmConnector index) {
  if (index < DRM_MAIN || index >= DRM_MAX || !drm[index].monitor_connector || drm_fd < 0) return;
  if (blank == current_blank_state[index]) return;
  // TWRP: never expose a freed/in-flight buffer while switching connector or waking.
  auto& output = drm[index];
  const int drawing_buffer = output.current_buffer;
  const int buffer = output.displayed_buffer >= 0 ? output.displayed_buffer : drawing_buffer;
  output.current_buffer = buffer;
  int ret;
  if (legacy_modeset) {
    ret = SetLegacyCrtc(index, blank ? 0 : output.GRSurfaceDrms[buffer]->fb_id);
  } else {
    if (!blank && Initdisplay(index)) { output.current_buffer = drawing_buffer; return; }
    drmModeAtomicReqPtr request = drmModeAtomicAlloc();
    if (!request) { output.current_buffer = drawing_buffer; return; }
    ret = blank ? DrmDisableCrtc(request, index) : DrmEnableCrtc(request, index);
    if (!ret) ret = drmModeAtomicCommit(drm_fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
    drmModeAtomicFree(request);
  }
  output.current_buffer = drawing_buffer;
  if (!ret) {
    current_blank_state[index] = blank;
    if (!blank) {
      output.displayed_buffer = buffer;
      active_display = index;
      // TWRP: optional second-stage high refresh (307c02f1); keep takeover mode on wake.
      if (output.fast_mode_blob_id) SwitchToFastMode(index);
    }
  } else {
    printf("DRM blank failed: display=%d blank=%d rc=%d\n", index, blank, ret);
  }
}
#endif // TWRP


bool MinuiBackendDrm::HasMultipleConnectors() {
  return (drm[DRM_SEC].GRSurfaceDrms[0] && drm[DRM_SEC].GRSurfaceDrms[1]);
}

static drmModeCrtc* find_crtc_for_connector(int fd, drmModeRes* resources,
                                            drmModeConnector* connector) {
  // Find the encoder. If we already have one, just use it.
  drmModeEncoder* encoder;
  if (connector->encoder_id) {
    encoder = drmModeGetEncoder(fd, connector->encoder_id);
  } else {
    encoder = nullptr;
  }

  int32_t crtc;
  if (encoder && encoder->crtc_id) {
    crtc = encoder->crtc_id;
    drmModeFreeEncoder(encoder);
    return drmModeGetCrtc(fd, crtc);
  }

  // Didn't find anything, try to find a crtc and encoder combo.
  crtc = -1;
  for (int i = 0; i < connector->count_encoders; i++) {
    encoder = drmModeGetEncoder(fd, connector->encoders[i]);

    if (encoder) {
      for (int j = 0; j < resources->count_crtcs; j++) {
        if (!(encoder->possible_crtcs & (1 << j))) continue;
        crtc = resources->crtcs[j];
        break;
      }
      if (crtc >= 0) {
        drmModeFreeEncoder(encoder);
        return drmModeGetCrtc(fd, crtc);
      }
    }
  }

  return nullptr;
}

std::vector<drmModeConnector*> find_used_connector_by_type(int fd, drmModeRes* resources,
                                                           unsigned type) {
  std::vector<drmModeConnector*> drmConnectors;
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector) {
      if ((connector->connector_type == type) && (connector->connection == DRM_MODE_CONNECTED) &&
          (connector->count_modes > 0)) {
        drmConnectors.push_back(connector);
      } else {
        drmModeFreeConnector(connector);
      }
    }
  }
  return drmConnectors;
}

static drmModeConnector* find_first_connected_connector(int fd, drmModeRes* resources) {
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector;

    connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector) {
      if ((connector->count_modes > 0) && (connector->connection == DRM_MODE_CONNECTED))
        return connector;

      drmModeFreeConnector(connector);
    }
  }
  return nullptr;
}

// TWRP BEGIN: kernel mode selection and opt-in high refresh (862d597b, 4688c0bc).
static bool get_kernel_video_mode(uint32_t* width, uint32_t* height) {
    FILE* cmdline = fopen("/proc/cmdline", "r");
    if (!cmdline)
        return false;

    char buffer[4096] = {};
    size_t length = fread(buffer, 1, sizeof(buffer) - 1, cmdline);
    fclose(cmdline);
    buffer[length] = '\0';

    static constexpr char kQemuVideoPrefix[] = "video=Virtual-1:";
    char* saveptr = nullptr;
    for (char* token = strtok_r(buffer, " \t\r\n", &saveptr); token;
         token = strtok_r(nullptr, " \t\r\n", &saveptr)) {
        if (strncmp(token, kQemuVideoPrefix, sizeof(kQemuVideoPrefix) - 1) != 0)
            continue;

        unsigned requested_width = 0;
        unsigned requested_height = 0;
        if (sscanf(token + sizeof(kQemuVideoPrefix) - 1, "%ux%u",
                   &requested_width, &requested_height) == 2 &&
            requested_width > 0 && requested_height > 0) {
            *width = requested_width;
            *height = requested_height;
            return true;
        }
    }
    return false;
}

static bool pick_fastest_mode(drmModeConnector* connector, uint32_t width, uint32_t height,
                              uint32_t* mode_index) {
    bool found = false;
    uint32_t best_refresh = 0;
    for (int modes = 0; modes < connector->count_modes; modes++) {
        const drmModeModeInfo& mode = connector->modes[modes];
        if (mode.hdisplay != width || mode.vdisplay != height)
            continue;
        if (!found || mode.vrefresh > best_refresh) {
            found = true;
            best_refresh = mode.vrefresh;
            *mode_index = modes;
        }
    }
    return found;
}

// TWRP END

bool MinuiBackendDrm::FindAndSetMonitor(int fd, drmModeRes* resources) {
  /* Look for LVDS/eDP/DSI connectors. Those are the main screens. */
  static constexpr unsigned kConnectorPriority[] = {
    DRM_MODE_CONNECTOR_LVDS,
    DRM_MODE_CONNECTOR_eDP,
    DRM_MODE_CONNECTOR_DSI,
  };

  std::vector<drmModeConnector*> drmConnectors;
  for (int i = 0; i < arraysize(kConnectorPriority) && drmConnectors.size() < DRM_MAX; i++) {
    auto connectors = find_used_connector_by_type(fd, resources, kConnectorPriority[i]);
    for (auto connector : connectors) {
      drmConnectors.push_back(connector);
      if (drmConnectors.size() >= DRM_MAX) break;
    }
  }

  /* If we didn't find a connector, grab the first one that is connected. */
  if (drmConnectors.empty()) {
    drmModeConnector* connector = find_first_connected_connector(fd, resources);
    if (connector) {
      drmConnectors.push_back(connector);
    }
  }

  for (int drm_index = 0; drm_index < drmConnectors.size(); drm_index++) {
    drm[drm_index].monitor_connector = drmConnectors[drm_index];

    drm[drm_index].selected_mode = 0;
    for (int modes = 0; modes < drmConnectors[drm_index]->count_modes; modes++) {
      printf("Display Mode %d resolution: %d x %d @ %d FPS\n", modes,
             drmConnectors[drm_index]->modes[modes].hdisplay,
             drmConnectors[drm_index]->modes[modes].vdisplay,
             drmConnectors[drm_index]->modes[modes].vrefresh);
      if (drmConnectors[drm_index]->modes[modes].type & DRM_MODE_TYPE_PREFERRED) {
        printf("Choosing display mode #%d\n", modes);
        drm[drm_index].selected_mode = modes;
        break;
      }
    }
  }

  // TWRP: QEMU's kernel-requested resolution takes precedence over preferred.
  uint32_t requested_width, requested_height;
  if (legacy_modeset && !drmConnectors.empty() &&
      get_kernel_video_mode(&requested_width, &requested_height)) {
    for (int mode = 0; mode < drm[DRM_MAIN].monitor_connector->count_modes; ++mode) {
      const auto& candidate = drm[DRM_MAIN].monitor_connector->modes[mode];
      if (candidate.hdisplay == requested_width && candidate.vdisplay == requested_height) {
        drm[DRM_MAIN].selected_mode = mode;
        break;
      }
    }
  }
  return drmConnectors.size() > 0;
}

#if 0 // TWRP: unmodified minui reference; retained for future resyncs.
void MinuiBackendDrm::DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc) {
  uint32_t prop_id;
  int ret = 0;
  drmModeAtomicReqPtr atomic_req = drmModeAtomicAlloc();

  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
    drmModeCrtc* crtc = find_crtc_for_connector(fd, resources, connector);
    if (crtc->crtc_id != main_crtc->crtc_id) {
      // Switching to atomic commit. Given only crtc, we can only set ACTIVE = 0
      // to disable any Nonmain CRTCs
      find_prop_id(&crtc_res, crtc, Crtc, crtc->crtc_id, "ACTIVE", prop_id, DRM_SEC);
      if (prop_id == 0)
        return;

      if (drmModeAtomicAddProperty(atomic_req, drm[DRM_SEC].monitor_crtc->crtc_id, prop_id, 0) < 0)
        return;
    }
    drmModeFreeCrtc(crtc);
  }

  ret = drmModeAtomicCommit(drm_fd, atomic_req,DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
  if (ret)
    printf("Atomic Commit failed in DisableNonMainCrtcs\n");

  drmModeAtomicFree(atomic_req);
}
#else // TWRP: active implementation below preserves GUI2/device requirements.
void MinuiBackendDrm::DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc) {
  // TWRP: CLO handoff must tolerate missing encoders/secondary CRTCs and release the request.
  for (int i = 0; i < resources->count_crtcs; ++i) {
    const uint32_t id = resources->crtcs[i];
    if (id == main_crtc->crtc_id) continue;
    if (legacy_modeset) { drmModeSetCrtc(fd, id, 0, 0, 0, nullptr, 0, nullptr); continue; }
    auto* props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_CRTC);
    if (!props) continue;
    auto* request = drmModeAtomicAlloc();
    if (request) {
      for (uint32_t j = 0; j < props->count_props; ++j) {
        auto* property = drmModeGetProperty(fd, props->props[j]);
        if (property) {
          if (!strcmp(property->name, "ACTIVE")) {
            drmModeAtomicAddProperty(request, id, property->prop_id, 0);
            drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
          }
          drmModeFreeProperty(property);
        }
      }
      drmModeAtomicFree(request);
    }
    drmModeFreeObjectProperties(props);
  }
}
#endif // TWRP


int MinuiBackendDrm::UpdatePlaneFB(DrmConnector index, uint32_t flags, void* user_data) {

  // TWRP: isolate CLO pipeline state between main and secondary connectors.
  auto& conn_res = drm[index].conn_res;
  auto& plane_res = drm[index].plane_res;
  auto& number_of_lms = drm[index].number_of_lms;
  auto& fb_prop_id = drm[index].fb_prop_id;

  drmModeAtomicReqPtr atomic_req = drmModeAtomicAlloc();
  if (!atomic_req) return -ENOMEM;
  uint32_t prop_id;
  // CLO: association is required by quadpipe topologies.
  add_prop(&conn_res, connector, Connector, drm[index].monitor_connector->connector_id,
           "CRTC_ID", drm[index].monitor_crtc->crtc_id, index);
  int ret = 0;
  for (uint32_t i = 0; i < number_of_lms && !ret; ++i) {
    if (drmModeAtomicAddProperty(atomic_req, plane_res[i].plane->plane_id,
          fb_prop_id, drm[index].GRSurfaceDrms[drm[index].current_buffer]->fb_id) < 0) ret = -EINVAL;
  }
  if (!ret) ret = drmModeAtomicCommit(drm_fd, atomic_req, flags, user_data);
  drmModeAtomicFree(atomic_req);
  return ret;
}

GRSurface* MinuiBackendDrm::Init() {
  int ret = 0;
  drmModeRes* res = nullptr;
  drm_fd = -1;

  // TWRP: per-connector initialization; CLO atomic/topology support stays the baseline.
  for (auto& output : drm) {
    output.spr_enabled = android::base::GetIntProperty("vendor.display.enable_spr", 0);
  }
  /* Consider DRM devices in order. */
  for (int i = 0; i < DRM_MAX_MINOR; i++) {
    auto dev_name = android::base::StringPrintf(DRM_DEV_NAME, DRM_DIR_NAME, i);
    android::base::unique_fd fd(open(dev_name.c_str(), O_RDWR | O_CLOEXEC));
    if (fd == -1) continue;

    /* We need dumb buffers. */
    if (uint64_t cap = 0; drmGetCap(fd.get(), DRM_CAP_DUMB_BUFFER, &cap) != 0 || cap == 0) {
      continue;
    }

    res = drmModeGetResources(fd.get());
    if (!res) {
      continue;
    }

    /* Use this device if it has at least one connected monitor. */
    if (res->count_crtcs > 0 && res->count_connectors > 0) {
      if (auto* connector = find_first_connected_connector(fd.get(), res)) {
        drmModeFreeConnector(connector); // TWRP: discovery probe is not the selected connector.
        drm_fd = fd.release();
        break;
      }
    }

    drmModeFreeResources(res);
    res = nullptr;
  }

  if (drm_fd == -1 || res == nullptr) {
    perror("Failed to find/open a drm device");
    return nullptr;
  }

  // TWRP: virtio uses legacy KMS rather than SDE atomic properties (862d597b).
  if (auto* version = drmGetVersion(drm_fd)) {
    legacy_modeset = version->name && !strcmp(version->name, "virtio_gpu");
    drmFreeVersion(version);
  }
  if (!FindAndSetMonitor(drm_fd, res)) {
    fprintf(stderr, "Failed to find main monitor_connector\n");
    drmModeFreeResources(res);
    return nullptr;
  }

  for (int i = 0; i < DRM_MAX; i++) {
    if (drm[i].monitor_connector) {
      drm[i].monitor_crtc = find_crtc_for_connector(drm_fd, res, drm[i].monitor_connector);
      if (!drm[i].monitor_crtc) {
        fprintf(stderr, "Failed to find monitor_crtc, drm index=%d\n", i);
        drmModeFreeResources(res);
        return nullptr;
      }

      for (int crtc = 0; crtc < res->count_crtcs; ++crtc)
        if (res->crtcs[crtc] == drm[i].monitor_crtc->crtc_id) drm[i].crtc_index = crtc;
      drm[i].monitor_crtc->mode = drm[i].monitor_connector->modes[drm[i].selected_mode];
      // TWRP: high refresh is opt-in and must keep the takeover resolution.
      uint32_t fastest = drm[i].selected_mode;
      const auto& mode = drm[i].monitor_crtc->mode;
      if (!legacy_modeset && android::base::GetBoolProperty("twrp.drm.fast_mode", false) &&
          pick_fastest_mode(drm[i].monitor_connector, mode.hdisplay, mode.vdisplay, &fastest) &&
          drm[i].monitor_connector->modes[fastest].vrefresh > mode.vrefresh) {
        drm[i].fast_mode = drm[i].monitor_connector->modes[fastest];
#ifndef TW_DRM_TWO_STAGE_MODESET
        // TWRP: one-stage modeset is the default; two-stage is a BoardConfig option.
        drm[i].monitor_crtc->mode = drm[i].fast_mode;
        drm[i].fast_mode = {};
#endif
      }

      int width = drm[i].monitor_crtc->mode.hdisplay;
      int height = drm[i].monitor_crtc->mode.vdisplay;

      drm[i].GRSurfaceDrms[0] = GRSurfaceDrm::Create(drm_fd, width, height);
      drm[i].GRSurfaceDrms[1] = GRSurfaceDrm::Create(drm_fd, width, height);
      if (!drm[i].GRSurfaceDrms[0] || !drm[i].GRSurfaceDrms[1]) {
        fprintf(stderr, "Failed to create GRSurfaceDrm, drm index=%d\n", i);
        drmModeFreeResources(res);
        return nullptr;
      }

      // TWRP: optional RAM staging, or damage synchronization after direct scanout.
      drm[i].direct_scanout = legacy_modeset ||
          android::base::GetBoolProperty("twrp.drm.direct_scanout", true);
      if (!drm[i].direct_scanout) {
        drm[i].draw_buf = GRSurface::Create(width, height, drm[i].GRSurfaceDrms[0]->row_bytes,
                                           drm[i].GRSurfaceDrms[0]->pixel_bytes);
        if (!drm[i].draw_buf) { drmModeFreeResources(res); return nullptr; }
        memset(drm[i].draw_buf->data(), 0, drm[i].draw_buf->row_bytes * height);
      }
      drm[i].pending_damage[0] = drm[i].pending_damage[1] = {0, 0, width, height};
      drm[i].direct_pending_damage = {0, 0, width, height};
      drm[i].current_buffer = 0;
    }
  }

  DisableNonMainCrtcs(drm_fd, res, drm[DRM_MAIN].monitor_crtc);

  drmModeFreeResources(res);

  drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);

  ret = legacy_modeset ? 0 : Initdisplay(DRM_MAIN);
  if (ret) {
    printf("Failed to init display\n");
    return NULL;
  }

  current_blank_state[DRM_MAIN] = true;
  current_blank_state[DRM_SEC] = true;

  Blank(false);

  if (current_blank_state[DRM_MAIN]) return nullptr; // TWRP: do not accept failed takeover.
  gr_set_physical_size(drm[DRM_MAIN].monitor_connector->mmWidth,
                       drm[DRM_MAIN].monitor_connector->mmHeight);
  drm[DRM_MAIN].current_buffer = 1 - drm[DRM_MAIN].displayed_buffer;
  return drm[DRM_MAIN].direct_scanout
      ? drm[DRM_MAIN].GRSurfaceDrms[drm[DRM_MAIN].current_buffer].get()
      : drm[DRM_MAIN].draw_buf.get();
}

// TWRP BEGIN: partial presentation and virtio fallback (bf39691b, aff6ddb2, 862d597b).
namespace {
bool empty_damage(const GRRect& rect) {
  return rect.left >= rect.right || rect.top >= rect.bottom;
}
void merge_damage(GRRect* destination, const GRRect& damage) {
  if (empty_damage(damage)) return;
  if (empty_damage(*destination)) { *destination = damage; return; }
  destination->left = std::min(destination->left, damage.left);
  destination->top = std::min(destination->top, damage.top);
  destination->right = std::max(destination->right, damage.right);
  destination->bottom = std::max(destination->bottom, damage.bottom);
}
size_t copy_damage(const GRSurface* source, GRSurface* destination, GRRect damage) {
  const int width = std::min(source->width, destination->width);
  const int height = std::min(source->height, destination->height);
  damage.left = std::clamp(damage.left, 0, width);
  damage.right = std::clamp(damage.right, 0, width);
  damage.top = std::clamp(damage.top, 0, height);
  damage.bottom = std::clamp(damage.bottom, 0, height);
  if (empty_damage(damage) || source->pixel_bytes != destination->pixel_bytes) return 0;
  const size_t bytes = (damage.right - damage.left) * source->pixel_bytes;
  // TWRP: retain the contiguous full-row copy optimization from bf39691b.
  if (damage.left == 0 && bytes == source->row_bytes &&
      source->row_bytes == destination->row_bytes) {
    memcpy(destination->data() + damage.top * destination->row_bytes,
           source->data() + damage.top * source->row_bytes,
           bytes * (damage.bottom - damage.top));
    return bytes * (damage.bottom - damage.top);
  }
  for (int y = damage.top; y < damage.bottom; ++y) {
    memcpy(destination->data() + y * destination->row_bytes + damage.left * destination->pixel_bytes,
           source->data() + y * source->row_bytes + damage.left * source->pixel_bytes, bytes);
  }
  return bytes * (damage.bottom - damage.top);
}
void page_flip_handler(int, unsigned int, unsigned int, unsigned int, void* pending) {
  *static_cast<bool*>(pending) = false;
}
int wait_for_page_flip(int fd, bool* waiting) {
  drmEventContext context{};
  context.version = DRM_EVENT_CONTEXT_VERSION;
  context.page_flip_handler = page_flip_handler;
  while (*waiting) {
    pollfd event{fd, POLLIN, 0};
    int ret = TEMP_FAILURE_RETRY(poll(&event, 1, 1000));
    if (ret <= 0) return ret ? -errno : -ETIMEDOUT;
    if (!(event.revents & POLLIN)) return -EIO;
    if (drmHandleEvent(fd, &context)) return -errno;
  }
  return 0;
}
}  // namespace

int MinuiBackendDrm::SetLegacyCrtc(DrmConnector index, uint32_t fb_id) {
  auto& output = drm[index];
  uint32_t connector = output.monitor_connector->connector_id;
  return drmModeSetCrtc(drm_fd, output.monitor_crtc->crtc_id, fb_id, 0, 0,
                        fb_id ? &connector : nullptr, fb_id ? 1 : 0,
                        fb_id ? &output.monitor_crtc->mode : nullptr);
}

int MinuiBackendDrm::Present(DrmConnector index) {
  auto& output = drm[index];
  if (output.displayed_buffer == output.current_buffer) return 0;
  const uint32_t fb_id = output.GRSurfaceDrms[output.current_buffer]->fb_id;
  int ret = -EINVAL;
  if (output.page_flip) {
    // Event userdata lives with the backend, including after a timeout.
    output.waiting_for_flip = true;
    ret = legacy_modeset
        ? drmModePageFlip(drm_fd, output.monitor_crtc->crtc_id, fb_id,
                           DRM_MODE_PAGE_FLIP_EVENT, &output.waiting_for_flip)
        : UpdatePlaneFB(index, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT,
                         &output.waiting_for_flip);
    const bool submitted = ret == 0;
    if (submitted) ret = wait_for_page_flip(drm_fd, &output.waiting_for_flip);
    if (!ret) {
      output.page_flip_failures = 0;
    } else if (legacy_modeset || submitted || ++output.page_flip_failures >= 8) {
      // TWRP: keep the historical atomic retry tolerance, but never reuse event
      // userdata after an accepted flip timed out or failed to complete.
      output.page_flip = false;
      printf("TWRP: page flip failed rc=%d, using blocking presentation\n", ret);
    }
  }
  if (ret) ret = legacy_modeset ? SetLegacyCrtc(index, fb_id) : UpdatePlaneFB(index);
  if (!ret) output.displayed_buffer = output.current_buffer;
  return ret;
}

void MinuiBackendDrm::SwitchToFastMode(DrmConnector index) {
  auto& output = drm[index];
  auto* request = drmModeAtomicAlloc();
  if (!request) return;
  // TWRP: preserve CLO's conn-crtc association and the displayed FB during modeset.
  for (uint32_t i = 0; i < output.conn_res.props->count_props; ++i) {
    auto* property = output.conn_res.props_info[i];
    if (!strcmp(property->name, "CRTC_ID"))
      drmModeAtomicAddProperty(request, output.monitor_connector->connector_id,
                                property->prop_id, output.monitor_crtc->crtc_id);
  }
  for (uint32_t i = 0; i < output.number_of_lms; ++i)
    drmModeAtomicAddProperty(request, output.plane_res[i].plane->plane_id, output.fb_prop_id,
                              output.GRSurfaceDrms[output.displayed_buffer]->fb_id);
  int ret = -EINVAL;
  for (uint32_t i = 0; i < output.crtc_res.props->count_props; ++i) {
    auto* property = output.crtc_res.props_info[i];
    if (!strcmp(property->name, "MODE_ID")) {
      ret = drmModeAtomicAddProperty(request, output.monitor_crtc->crtc_id,
                                     property->prop_id, output.fast_mode_blob_id);
      break;
    }
  }
  if (ret >= 0) ret = drmModeAtomicCommit(drm_fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
  drmModeAtomicFree(request);
  if (ret) {
    printf("TWRP: high refresh modeset failed rc=%d, retaining takeover mode\n", ret);
    drmModeDestroyPropertyBlob(drm_fd, output.fast_mode_blob_id);
    output.fast_mode_blob_id = 0;
  }
}

#if 0 // TWRP: unmodified minui reference; retained for future resyncs.
GRSurface* MinuiBackendDrm::Flip() {
  UpdatePlaneFB(active_display);

  drm[active_display].current_buffer = 1 - drm[active_display].current_buffer;
  return drm[active_display].GRSurfaceDrms[drm[active_display].current_buffer].get();
}
#else // TWRP: active implementation below preserves GUI2/device requirements.
GRSurface* MinuiBackendDrm::Flip() {
  auto& output = drm[active_display];
  const GRRect damage = gr_get_damage();
  auto* drawing = output.direct_scanout
      ? output.GRSurfaceDrms[output.current_buffer].get() : output.draw_buf.get();
  size_t copied_bytes = 0;
  if (output.direct_scanout) {
    merge_damage(&output.direct_pending_damage, damage);
    if (empty_damage(output.direct_pending_damage))
      output.direct_pending_damage = {0, 0, static_cast<int>(drawing->width),
                                           static_cast<int>(drawing->height)};
    const int presented = output.current_buffer;
    int released = output.displayed_buffer;
    if (Present(active_display)) return drawing;
    if (released < 0 || released == presented) released = 1 - presented;
    copied_bytes = copy_damage(drawing, output.GRSurfaceDrms[released].get(),
                               output.direct_pending_damage);
    output.direct_pending_damage = {};
    output.current_buffer = released;
    drawing = output.GRSurfaceDrms[released].get();
  } else {
    merge_damage(&output.pending_damage[0], damage);
    merge_damage(&output.pending_damage[1], damage);
    copied_bytes = copy_damage(drawing, output.GRSurfaceDrms[output.current_buffer].get(),
                               output.pending_damage[output.current_buffer]);
    if (!Present(active_display)) {
      output.pending_damage[output.current_buffer] = {};
      output.current_buffer = 1 - output.current_buffer;
    }
  }
  ++output.stats_frames;
  output.stats_copied_bytes += copied_bytes;
  output.stats_full_bytes += drawing->height * drawing->row_bytes;
  if (output.stats_frames == 120) {
    if (android::base::GetBoolProperty("twrp.drm.stats", false))
      printf("TWRP DRM copy stats: direct=%d copied=%" PRIu64 "/%" PRIu64 " bytes\n",
             output.direct_scanout, output.stats_copied_bytes, output.stats_full_bytes);
    output.stats_frames = output.stats_copied_bytes = output.stats_full_bytes = 0;
  }
  return drawing;
}
#endif // TWRP

// TWRP END

// TWRP BEGIN: release every CLO pipeline object while its DRM fd is still open (b7457030).
void MinuiBackendDrm::ReleasePipeline(DrmConnector index) {
  auto& output = drm[index];
  output.pipeline_ready = false;
  auto free_properties = [](auto* object) {
    if (object->props_info) {
      for (uint32_t i = 0; i < object->props->count_props; ++i)
        if (object->props_info[i]) drmModeFreeProperty(object->props_info[i]);
      free(object->props_info);
      object->props_info = nullptr;
    }
    if (object->props) drmModeFreeObjectProperties(object->props);
    object->props = nullptr;
  };
  for (auto& plane : output.plane_res) {
    free_properties(&plane);
    if (plane.plane) drmModeFreePlane(plane.plane);
    plane.plane = nullptr;
  }
  free_properties(&output.conn_res);
  free_properties(&output.crtc_res);
  if (output.crtc_res.mode_blob_id) drmModeDestroyPropertyBlob(drm_fd, output.crtc_res.mode_blob_id);
  if (output.crtc_res.spr_blob_id) drmModeDestroyPropertyBlob(drm_fd, output.crtc_res.spr_blob_id);
  if (output.fast_mode_blob_id) drmModeDestroyPropertyBlob(drm_fd, output.fast_mode_blob_id);
  output.crtc_res.mode_blob_id = output.crtc_res.spr_blob_id = output.fast_mode_blob_id = 0;
}

#if 0 // TWRP: unmodified minui reference; retained for future resyncs.
MinuiBackendDrm::~MinuiBackendDrm() {
  for (int i = 0; i < DRM_MAX; i++) {
    if (drm[i].monitor_connector) {
      drmModeFreeCrtc(drm[i].monitor_crtc);
      drmModeFreeConnector(drm[i].monitor_connector);
    }
  }
  Blank(true);
  drmModeDestroyPropertyBlob(drm_fd, crtc_res.mode_blob_id);
  close(drm_fd);
  drm_fd = -1;
}
#else // TWRP: active implementation below preserves GUI2/device requirements.
MinuiBackendDrm::~MinuiBackendDrm() {
  if (drm_fd < 0) return;
  for (int i = DRM_MAIN; i < DRM_MAX; ++i) {
    auto index = static_cast<DrmConnector>(i);
    if (drm[i].monitor_connector) Blank(true, index);
    ReleasePipeline(index);
    for (auto& surface : drm[i].GRSurfaceDrms) surface.reset();
    drm[i].draw_buf.reset();
    if (drm[i].monitor_crtc) drmModeFreeCrtc(drm[i].monitor_crtc);
    if (drm[i].monitor_connector) drmModeFreeConnector(drm[i].monitor_connector);
    drm[i].monitor_crtc = nullptr;
    drm[i].monitor_connector = nullptr;
  }
  close(drm_fd);
  drm_fd = -1;
}
#endif // TWRP

// TWRP END
