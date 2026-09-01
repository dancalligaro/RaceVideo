#ifndef RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_
#define RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_

#include <filesystem>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
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
  std::vector<SpeedUnit> speed_units;
};

absl::StatusOr<VideoDimensions> DetermineOutputDimensions(
    const VideoInfo& video, int output_width);

absl::Status EncodeOverlayVideo(const TelemetryData& telemetry,
                                const OverlayData& overlay,
                                const VideoInfo& video,
                                const VideoEncodeOptions& options);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_
