#ifndef RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_
#define RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_

#include <filesystem>

#include "absl/status/status.h"
#include "ffmpeg/video_probe.h"
#include "overlay/display_options.h"
#include "overlay/overlay_data.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct VideoEncodeOptions {
  std::filesystem::path input_path;
  std::filesystem::path output_path;
  double start_seconds;
  double duration_seconds;
  SpeedUnit speed_unit;
};

absl::Status EncodeOverlayVideo(const TelemetryData& telemetry,
                                const OverlayData& overlay,
                                const VideoInfo& video,
                                const VideoEncodeOptions& options);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_VIDEO_ENCODER_H_
