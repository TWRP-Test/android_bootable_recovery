#include "webm_vp8.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>

#include "mkvmuxer/mkvmuxer.h"
#include "mkvmuxer/mkvwriter.h"
#include "vpx/vp8cx.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vpx_image.h"

namespace gui2_backend {
namespace {

constexpr size_t kMaxQueuedFrames = 2;
constexpr int kTargetBitrateKbps = 4000;
constexpr int kCpuUsed = 8;

uint64_t current_monotonic_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace

webm_vp8_recorder::~webm_vp8_recorder() {
  stop();
}

capture_result webm_vp8_recorder::start(const std::string& path, int width, int height,
                                        int frames_per_second, uint64_t start_timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (recording_) return { false, {}, "Recording is already active" };
  if (path.empty() || width <= 0 || height <= 0) {
    return { false, {}, "Invalid recording dimensions or path" };
  }

  file_ = std::fopen(path.c_str(), "wb");
  if (file_ == nullptr) return { false, {}, "Unable to create recording file" };

  path_ = path;
  width_ = width;
  height_ = height;
  frames_per_second_ = std::clamp(frames_per_second, 1, 60);
  frame_interval_ms_ = std::max<uint64_t>(1, 1000 / frames_per_second_);
  start_timestamp_ms_ = start_timestamp_ms == 0 ? current_monotonic_ms() : start_timestamp_ms;
  last_frame_timestamp_ms_ = 0;
  frame_number_ = 0;
  last_encoded_timestamp_ms_ = 0;
  track_number_ = 0;
  queue_.clear();
  last_frame_.clear();
  error_.clear();
  worker_failed_ = false;
  stop_requested_ = false;

  if (!initialize_codec()) {
    destroy_codec();
    std::fclose(file_);
    file_ = nullptr;
    return { false, {}, "Unable to initialize VP8 encoder" };
  }

  writer_ = new (std::nothrow) mkvmuxer::MkvWriter(file_);
  segment_ = new (std::nothrow) mkvmuxer::Segment();
  if (writer_ == nullptr || segment_ == nullptr || !segment_->Init(writer_)) {
    delete segment_;
    delete writer_;
    segment_ = nullptr;
    writer_ = nullptr;
    destroy_codec();
    std::fclose(file_);
    file_ = nullptr;
    return { false, {}, "Unable to initialize WebM muxer" };
  }

  segment_->set_mode(mkvmuxer::Segment::kFile);
  // Omit cues to reduce file overhead.
  segment_->OutputCues(false);
  segment_->set_max_cluster_duration(5000000000ULL);
  track_number_ = segment_->AddVideoTrack(width_, height_, 1);
  if (track_number_ == 0) {
    delete segment_;
    delete writer_;
    segment_ = nullptr;
    writer_ = nullptr;
    destroy_codec();
    std::fclose(file_);
    file_ = nullptr;
    return { false, {}, "Unable to initialize WebM video track" };
  }
  auto* track = static_cast<mkvmuxer::VideoTrack*>(segment_->GetTrackByNumber(track_number_));
  if (track != nullptr) {
    track->set_default_duration(1000000000ULL / static_cast<uint64_t>(frames_per_second_));
    track->set_frame_rate(static_cast<double>(frames_per_second_));
  }

  recording_ = true;
  worker_ = std::thread(&webm_vp8_recorder::worker_loop, this);
  return { true, path_, {} };
}

capture_result webm_vp8_recorder::stop(uint64_t end_timestamp_ms) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!recording_ && !worker_.joinable()) return { false, {}, "Recording is not active" };
    stop_requested_ = true;
  }
  condition_.notify_all();
  if (worker_.joinable()) worker_.join();

  std::lock_guard<std::mutex> lock(mutex_);
  if (end_timestamp_ms == 0) end_timestamp_ms = current_monotonic_ms();
  capture_result result{ !worker_failed_, path_, error_ };
  if (!finalize_file(end_timestamp_ms) && result.success) {
    result.success = false;
    result.error = "Unable to finalize WebM recording";
  }
  delete segment_;
  delete writer_;
  segment_ = nullptr;
  writer_ = nullptr;
  destroy_codec();
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  recording_ = false;
  stop_requested_ = false;
  queue_.clear();
  last_frame_.clear();
  return result;
}

bool webm_vp8_recorder::is_recording() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return recording_;
}

void webm_vp8_recorder::submit_frame(const frame_view& frame, uint64_t monotonic_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!recording_ || stop_requested_ || worker_failed_ || queue_.size() >= kMaxQueuedFrames ||
      frame.data == nullptr || frame.width != width_ || frame.height != height_ ||
      frame.row_bytes < frame.width * (frame.format == frame_pixel_format::RGB565 ? 2 : 4)) {
    return;
  }
  if (last_frame_timestamp_ms_ != 0 &&
      monotonic_ms < last_frame_timestamp_ms_ + frame_interval_ms_) {
    return;
  }
  last_frame_timestamp_ms_ = monotonic_ms;

  frame_packet packet;
  packet.width = frame.width;
  packet.height = frame.height;
  packet.row_bytes = frame.row_bytes;
  packet.format = frame.format;
  packet.timestamp_ms = monotonic_ms;
  packet.data.assign(frame.data, frame.data + static_cast<size_t>(frame.row_bytes) * frame.height);
  queue_.push_back(std::move(packet));
  condition_.notify_one();
}

void webm_vp8_recorder::set_error_locked(const std::string& error) {
  if (error_.empty()) error_ = error;
  worker_failed_ = true;
}

bool webm_vp8_recorder::initialize_codec() {
  codec_ = new (std::nothrow) vpx_codec_ctx_t();
  image_ = new (std::nothrow) vpx_image_t();
  if (codec_ == nullptr || image_ == nullptr) return false;
  std::memset(codec_, 0, sizeof(vpx_codec_ctx_t));
  std::memset(image_, 0, sizeof(vpx_image_t));

  vpx_codec_enc_cfg_t config;
  if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &config, 0) != VPX_CODEC_OK) return false;
  config.g_w = width_;
  config.g_h = height_;
  config.g_timebase.num = 1;
  config.g_timebase.den = frames_per_second_;
  config.g_threads = 2;
  config.g_lag_in_frames = 0;
  config.g_pass = VPX_RC_ONE_PASS;
  config.rc_end_usage = VPX_VBR;
  config.rc_target_bitrate = kTargetBitrateKbps;
  config.rc_min_quantizer = 4;
  config.rc_max_quantizer = 56;
  config.kf_mode = VPX_KF_AUTO;
  config.kf_max_dist = frames_per_second_ * 5;
  if (vpx_codec_enc_init(codec_, vpx_codec_vp8_cx(), &config, 0) != VPX_CODEC_OK) return false;
  if (vpx_codec_control(codec_, VP8E_SET_CPUUSED, kCpuUsed) != VPX_CODEC_OK) return false;
  if (vpx_codec_control(codec_, VP8E_SET_ENABLEAUTOALTREF, 0) != VPX_CODEC_OK) return false;

  image_ = vpx_img_alloc(image_, VPX_IMG_FMT_I420, width_, height_, 1);
  return image_ != nullptr;
}

void webm_vp8_recorder::destroy_codec() {
  if (codec_ != nullptr) {
    vpx_codec_destroy(codec_);
    delete codec_;
    codec_ = nullptr;
  }
  if (image_ != nullptr) {
    vpx_img_free(image_);
    delete image_;
    image_ = nullptr;
  }
}

void webm_vp8_recorder::worker_loop() {
  for (;;) {
    frame_packet packet;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [this] { return stop_requested_ || !queue_.empty(); });
      if (queue_.empty() && stop_requested_) return;
      packet = std::move(queue_.front());
      queue_.pop_front();
    }
    if (!encode_frame(packet)) {
      std::lock_guard<std::mutex> lock(mutex_);
      set_error_locked("Unable to encode WebM frame");
    }
  }
}

bool webm_vp8_recorder::encode_frame(const frame_packet& packet) {
  i420_frame yuv;
  const frame_view view{ packet.data.data(), packet.width, packet.height, packet.row_bytes,
                         packet.format };
  if (!rgb_to_i420(view, &yuv) || image_ == nullptr || codec_ == nullptr) return false;

  const int image_width = static_cast<int>(image_->w);
  const int image_height = static_cast<int>(image_->h);
  for (int y = 0; y < image_height; ++y) {
    const int source_y = std::min(packet.height - 1, y);
    uint8_t* destination =
        image_->planes[VPX_PLANE_Y] + static_cast<size_t>(y) * image_->stride[VPX_PLANE_Y];
    const uint8_t* source = yuv.y() + static_cast<size_t>(source_y) * yuv.y_stride;
    std::memcpy(destination, source, packet.width);
    for (int x = packet.width; x < image_width; ++x) destination[x] = source[packet.width - 1];
  }
  for (int y = 0; y < image_height / 2; ++y) {
    const int source_y = std::min(yuv.chroma_height - 1, y);
    const uint8_t* source_u = yuv.u() + static_cast<size_t>(source_y) * yuv.u_stride;
    const uint8_t* source_v = yuv.v() + static_cast<size_t>(source_y) * yuv.v_stride;
    uint8_t* destination_u =
        image_->planes[VPX_PLANE_U] + static_cast<size_t>(y) * image_->stride[VPX_PLANE_U];
    uint8_t* destination_v =
        image_->planes[VPX_PLANE_V] + static_cast<size_t>(y) * image_->stride[VPX_PLANE_V];
    std::memcpy(destination_u, source_u, yuv.chroma_width);
    std::memcpy(destination_v, source_v, yuv.chroma_width);
    for (int x = yuv.chroma_width; x < image_width / 2; ++x) {
      destination_u[x] = source_u[yuv.chroma_width - 1];
      destination_v[x] = source_v[yuv.chroma_width - 1];
    }
  }
  image_->cs = VPX_CS_SRGB;
  image_->range = VPX_CR_STUDIO_RANGE;

  if (vpx_codec_encode(codec_, image_, static_cast<vpx_codec_pts_t>(frame_number_++), 1, 0,
                       VPX_DL_REALTIME) != VPX_CODEC_OK) {
    return false;
  }

  vpx_codec_iter_t iterator = nullptr;
  const vpx_codec_cx_pkt_t* packet_out;
  bool wrote_packet = false;
  while ((packet_out = vpx_codec_get_cx_data(codec_, &iterator)) != nullptr) {
    if (packet_out->kind != VPX_CODEC_CX_FRAME_PKT) continue;
    const bool key_frame = (packet_out->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
    if (!write_encoded_frame(static_cast<const uint8_t*>(packet_out->data.frame.buf),
                             packet_out->data.frame.sz, key_frame, packet.timestamp_ms)) {
      return false;
    }
    wrote_packet = true;
  }
  return wrote_packet;
}

bool webm_vp8_recorder::write_encoded_frame(const uint8_t* data, size_t size, bool key_frame,
                                            uint64_t timestamp_ms) {
  if (segment_ == nullptr || data == nullptr || size == 0 || size > UINT32_MAX) return false;
  const uint64_t relative_ms =
      timestamp_ms >= start_timestamp_ms_ ? timestamp_ms - start_timestamp_ms_ : 0;
  const uint64_t timestamp_ns = relative_ms * 1000000ULL;
  if (last_encoded_timestamp_ms_ != 0 && timestamp_ms <= last_encoded_timestamp_ms_) return true;
  if (!segment_->AddFrame(data, static_cast<uint64_t>(size), track_number_, timestamp_ns,
                          key_frame)) {
    return false;
  }
  last_encoded_timestamp_ms_ = timestamp_ms;
  return true;
}

bool webm_vp8_recorder::finalize_file(uint64_t end_timestamp_ms) {
  if (segment_ == nullptr) return false;
  const uint64_t duration_ms = end_timestamp_ms > start_timestamp_ms_
                                   ? end_timestamp_ms - start_timestamp_ms_
                                   : frame_interval_ms_;
  segment_->set_duration(static_cast<double>(std::max<uint64_t>(duration_ms, frame_interval_ms_)));
  return segment_->Finalize() && file_ != nullptr && std::fflush(file_) == 0;
}

}  // namespace gui2_backend
