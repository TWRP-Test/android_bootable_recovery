#ifndef GUI2_BACKEND_WEBM_VP8_H
#define GUI2_BACKEND_WEBM_VP8_H

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rgb_to_i420.h"

struct vpx_codec_ctx;
struct vpx_image;

namespace mkvmuxer {
class MkvWriter;
class Segment;
}  // namespace mkvmuxer

namespace gui2_backend {

class webm_vp8_recorder final {
 public:
  webm_vp8_recorder() = default;
  ~webm_vp8_recorder();

  webm_vp8_recorder(const webm_vp8_recorder&) = delete;
  webm_vp8_recorder& operator=(const webm_vp8_recorder&) = delete;

  capture_result start(const std::string& path, int width, int height, int frames_per_second,
                       uint64_t start_timestamp_ms = 0);
  capture_result stop(uint64_t end_timestamp_ms = 0);
  bool is_recording() const;
  void submit_frame(const frame_view& frame, uint64_t monotonic_ms);

 private:
  struct frame_packet {
    int width = 0;
    int height = 0;
    int row_bytes = 0;
    frame_pixel_format format = frame_pixel_format::BGRA8888;
    std::vector<uint8_t> data;
    uint64_t timestamp_ms = 0;
  };

  void worker_loop();
  bool encode_frame(const frame_packet& packet);
  bool write_encoded_frame(const uint8_t* data, size_t size, bool key_frame, uint64_t timestamp_ms);
  bool initialize_codec();
  void destroy_codec();
  bool finalize_file(uint64_t end_timestamp_ms);
  void set_error_locked(const std::string& error);

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<frame_packet> queue_;
  std::thread worker_;
  bool recording_ = false;
  bool stop_requested_ = false;
  bool worker_failed_ = false;
  std::string error_;
  std::string path_;
  FILE* file_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int frames_per_second_ = 30;
  uint64_t frame_interval_ms_ = 1000 / 30;
  uint64_t start_timestamp_ms_ = 0;
  uint64_t last_frame_timestamp_ms_ = 0;
  uint64_t frame_number_ = 0;
  uint64_t last_encoded_timestamp_ms_ = 0;
  uint64_t track_number_ = 0;
  std::vector<uint8_t> last_frame_;

  vpx_codec_ctx* codec_ = nullptr;
  vpx_image* image_ = nullptr;
  mkvmuxer::MkvWriter* writer_ = nullptr;
  mkvmuxer::Segment* segment_ = nullptr;
};

}  // namespace gui2_backend

#endif
