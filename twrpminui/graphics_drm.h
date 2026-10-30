/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Baseline: current minui/graphics_drm.h; local differences are marked TWRP.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <map>
#include <memory>

#include <xf86drmMode.h>

#include "graphics.h"
#include "twrpminui/minui.h"

#define NUM_MAIN 1
#define NUM_PLANES 4
#define DEFAULT_NUM_LMS 2

#define SPR_INIT_PARAM_SIZE_1 4
#define SPR_INIT_PARAM_SIZE_2 5
#define SPR_INIT_PARAM_SIZE_3 16
#define SPR_INIT_PARAM_SIZE_4 24
#define SPR_INIT_PARAM_SIZE_5 32
#define SPR_INIT_PARAM_SIZE_6 7

enum class SPRPackType {
  kPentile,
  kRGBW,
  kYYGW,
  kYYGM,
  kDelta3,
  kMax = 0xFF,
};

enum class SPRFilterType {
  kPixelDrop,
  kBilinear,
  kFourTap,
  kAdaptive,
  k2DAvg,
  kMax = 0xFF,
};

enum class SPRAdaptiveModeType {
  kYYGM,
  kYYGW,
  kMax = 0xFF,
};

static const std::map<SPRPackType, uint32_t> kDefaultColorPhaseIncrement = {
  { { { SPRPackType::kPentile }, { 8 } },
    { { SPRPackType::kYYGM }, { 6 } },
    { { SPRPackType::kYYGW }, { 6 } },
    { { SPRPackType::kDelta3 }, { 6 } },
    { { SPRPackType::kRGBW }, { 8 } } }
};
static const std::map<SPRPackType, uint32_t> kDefaultColorPhaseRepeat = {
  { { { SPRPackType::kPentile }, { 2 } },
    { { SPRPackType::kYYGM }, { 2 } },
    { { SPRPackType::kYYGW }, { 2 } },
    { { SPRPackType::kDelta3 }, { 2 } },
    { { SPRPackType::kRGBW }, { 2 } } }
};
static const std::map<SPRPackType, std::array<uint16_t, SPR_INIT_PARAM_SIZE_1>> kDecimationRatioMap{
  {
      { { SPRPackType::kPentile }, { 1, 0, 1, 0 } },
      { { SPRPackType::kYYGM }, { 2, 2, 2, 0 } },
      { { SPRPackType::kYYGW }, { 2, 2, 2, 0 } },
      { { SPRPackType::kRGBW }, { 1, 1, 1, 1 } },
  }
};
static const std::map<SPRFilterType, std::array<int16_t, SPR_INIT_PARAM_SIZE_3>>
    kDefaultFilterCoeffsMap{
      { { { SPRFilterType::kPixelDrop }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { { SPRFilterType::kBilinear },
          { 0, 512, 0, 0, -33, 443, 110, -8, -23, 279, 279, -23, -8, 110, 443, -33 } },
        { { SPRFilterType::kFourTap },
          { 128, 256, 128, 0, 86, 241, 164, 21, 52, 204, 204, 52, 21, 164, 241, 86 } },
        { { SPRFilterType::kAdaptive },
          { 0, 256, 256, 0, 0, 256, 256, 0, 0, 256, 256, 0, 0, 256, 256, 0 } } }
    };
static const std::map<SPRPackType, std::array<int16_t, SPR_INIT_PARAM_SIZE_4>>
    kDefaultColorPhaseMap{
      { { { SPRPackType::kPentile },
          { -2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { { SPRPackType::kYYGM },
          { -3, 0, 0, 0, 0, 0, -1, 2, 1, 1, 0, 0, 1, -2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { { SPRPackType::kYYGW },
          { -4, 2, 0, 0, 0, -1, 2, 2, 0, -1, -1, -1, 2, 2, -1, -1, -1, 2, 0, 0, 0, 0, 0, 0 } },
        { { SPRPackType::kDelta3 },
          { -3, 0, 0, 0, 0, 0, 0, -3, 0, 0, 0, 0, -3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { { SPRPackType::kRGBW },
          { -4, 0, 0, 0, 0, 0, -2, 2, 0, 0, 0, 0, 0, -4, 0, 0, 0, 0, 2, -2, 0, 0, 0, 0 } } }
    };
static const std::array<uint16_t, SPR_INIT_PARAM_SIZE_1> kDefaultRGBWGains = { 1024, 1024, 1024,
                                                                               341 };
static const std::array<uint16_t, SPR_INIT_PARAM_SIZE_1> kDefaultOPRGains = { 341, 341, 341, 0 };
static const std::array<uint16_t, SPR_INIT_PARAM_SIZE_2> kDefaultAdaptiveStrengths = { 0, 4, 8, 12,
                                                                                       16 };
static const std::array<uint16_t, SPR_INIT_PARAM_SIZE_5> kDefaultOPROffsets = {
  0,    132,  264,  396,  529,  661,  793,  925,  1057, 1189, 1321, 1453, 1586, 1718, 1850, 1982,
  2114, 2246, 2378, 2510, 2643, 2775, 2907, 3039, 3171, 3303, 3435, 3567, 3700, 3832, 3964, 4095
};

struct Crtc {
  drmModeObjectProperties *props;
  drmModePropertyRes **props_info;
  uint32_t mode_blob_id;
  uint32_t spr_blob_id;
};

struct Connector {
  drmModeObjectProperties *props;
  drmModePropertyRes **props_info;
};

struct Plane {
  drmModePlane *plane;
  drmModeObjectProperties *props;
  drmModePropertyRes ** props_info;
};

struct drm_msm_spr_init_cfg {
  __u64 flags;
  __u16 cfg0;
  __u16 cfg1;
  __u16 cfg2;
  __u16 cfg3;
  __u16 cfg4;
  __u16 cfg5;
  __u16 cfg6;
  __u16 cfg7;
  __u16 cfg8;
  __u16 cfg9;
  __u32 cfg10;
  __u16 cfg11[SPR_INIT_PARAM_SIZE_1];
  __u16 cfg12[SPR_INIT_PARAM_SIZE_1];
  __u16 cfg13[SPR_INIT_PARAM_SIZE_1];
  __u16 cfg14[SPR_INIT_PARAM_SIZE_2];
  __u16 cfg15[SPR_INIT_PARAM_SIZE_5];
  int cfg16[SPR_INIT_PARAM_SIZE_3];
  int cfg17[SPR_INIT_PARAM_SIZE_4];
};

struct drm_msm_spr_init_cfg_v2 {
  __u64 flags;
  __u16 cfg0;
  __u16 cfg1;
  __u16 cfg2;
  __u16 cfg3;
  __u16 cfg4;
  __u16 cfg5;
  __u16 cfg6;
  __u16 cfg7;
  __u16 cfg8;
  __u16 cfg9;
  __u32 cfg10;
  __u16 cfg11[SPR_INIT_PARAM_SIZE_1];
  __u16 cfg12[SPR_INIT_PARAM_SIZE_1];
  __u16 cfg13[SPR_INIT_PARAM_SIZE_1];
  __u16 cfg14[SPR_INIT_PARAM_SIZE_2];
  __u16 cfg15[SPR_INIT_PARAM_SIZE_5];
  int cfg16[SPR_INIT_PARAM_SIZE_3];
  int cfg17[SPR_INIT_PARAM_SIZE_4];
  __u16 cfg18_en;
  __u8 cfg18[SPR_INIT_PARAM_SIZE_6];
};

class GRSurfaceDrm : public GRSurface {
 public:
  ~GRSurfaceDrm() override;

  // Creates a GRSurfaceDrm instance.
  static std::unique_ptr<GRSurfaceDrm> Create(int drm_fd, int width, int height);

  uint8_t* data() override {
    return mmapped_buffer_;
  }

 private:
  friend class MinuiBackendDrm;

  GRSurfaceDrm(size_t width, size_t height, size_t row_bytes, size_t pixel_bytes, int drm_fd,
               uint32_t handle)
      : GRSurface(width, height, row_bytes, pixel_bytes), drm_fd_(drm_fd), handle(handle) {}

  const int drm_fd_;

  uint32_t fb_id{ 0 };
  uint32_t handle{ 0 };
  uint8_t* mmapped_buffer_{ nullptr };
};

class MinuiBackendDrm : public MinuiBackend {
 public:
  MinuiBackendDrm() = default;
  ~MinuiBackendDrm() override;

  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  void Blank(bool blank, DrmConnector index) override;
  bool HasMultipleConnectors() override;

 private:
  int DrmDisableCrtc(drmModeAtomicReqPtr atomic_req, DrmConnector index);
  int DrmEnableCrtc(drmModeAtomicReqPtr atomic_req, DrmConnector index);
  bool DrmEnableCrtc(int drm_fd, drmModeCrtc* crtc, const std::unique_ptr<GRSurfaceDrm>& surface,
                     uint32_t* conntcors);
  void DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc);
  int SetupPipeline(drmModeAtomicReqPtr atomic_req, DrmConnector index);
  int TeardownPipeline(drmModeAtomicReqPtr atomic_req, DrmConnector index);
  // TWRP: report failures and support event-driven presentation without a modeset.
  int UpdatePlaneFB(DrmConnector index, uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET,
                    void* user_data = nullptr);
  // TWRP: keep lifecycle, partial presentation and QEMU fallback per connector.
  void ReleasePipeline(DrmConnector index);
  int Present(DrmConnector index);
  int SetLegacyCrtc(DrmConnector index, uint32_t fb_id);
  void SwitchToFastMode(DrmConnector index);
  int AtomicPopulatePlane(int plane, drmModeAtomicReqPtr atomic_req, DrmConnector index);
  bool FindAndSetMonitor(int fd, drmModeRes* resources);
  int Initdisplay(DrmConnector index);

  struct DrmInterface {
    std::unique_ptr<GRSurfaceDrm> GRSurfaceDrms[2];
    int current_buffer{ 0 };
    drmModeCrtc* monitor_crtc{ nullptr };
    drmModeConnector* monitor_connector{ nullptr };
    uint32_t selected_mode{ 0 };
    // TWRP: CLO pipeline metadata belongs to each connector, including on wake.
    int fb_prop_id = 0;
    struct Crtc crtc_res{};
    struct Connector conn_res{};
    struct Plane plane_res[NUM_PLANES]{};
    uint32_t number_of_lms = DEFAULT_NUM_LMS;
    uint32_t spr_enabled = 0;
    std::string spr_prop_name;
    std::string demura_prop_name;
    bool pipeline_ready = false;
    int crtc_index = 0;
    int displayed_buffer = -1;
    bool direct_scanout = true;
    bool page_flip = true;
    bool waiting_for_flip = false;
    int page_flip_failures = 0;
    std::unique_ptr<GRSurface> draw_buf;
    GRRect pending_damage[2]{};
    GRRect direct_pending_damage{};
    drmModeModeInfo fast_mode{};
    uint32_t fast_mode_blob_id = 0;
    uint64_t stats_frames = 0, stats_copied_bytes = 0, stats_full_bytes = 0;

  } drm[DRM_MAX];

  int drm_fd{ -1 };
  DrmConnector active_display = DRM_MAIN;
  bool current_blank_state[DRM_MAX] = {true, true}; // TWRP: safe during failed Init cleanup.
  // TWRP: virtio lacks the SDE atomic pipeline (862d597b).
  bool legacy_modeset = false;

};
