#ifndef RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_
#define RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_

#include <filesystem>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "ffmpeg/process.h"
#include "ffmpeg/video_probe.h"
#include "overlay/display_options.h"
#include "overlay/overlay_data.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct VideoDimensions {
  int width;
  int height;

  bool operator==(const VideoDimensions&) const = default;
};

struct VideoChapter {
  std::filesystem::path path;
  double duration_seconds;
};

struct VideoEncodeOptions {
  std::vector<VideoChapter> chapters;
  std::filesystem::path output_path;
  double start_seconds;
  double duration_seconds;
  int output_width;
  VideoEncoder video_encoder;
  VideoPipeline video_pipeline;
  std::filesystem::path vaapi_device = "/dev/dri/renderD128";
  std::vector<SpeedUnit> speed_units;
  int overlay_workers = 0;
};

absl::StatusOr<VideoDimensions> DetermineOutputDimensions(
    const VideoInfo& video, int output_width);

// Streams overlays in frame order. A synchronous sink must consume the bytes
// before returning. The reference path is retained for renderer benchmarks.
absl::Status RenderOverlayFrames(const TelemetryData& telemetry,
                                 const OverlayData& overlay,
                                 const VideoInfo& video,
                                 const VideoEncodeOptions& options,
                                 int frame_count, const ByteSink& sink,
                                 bool use_cache = true);

absl::Status EncodeOverlayVideo(const TelemetryData& telemetry,
                                const OverlayData& overlay,
                                const VideoInfo& video,
                                const VideoEncodeOptions& options);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_
