#ifndef RACEVIDEO_FFMPEG_VIDEO_PROBE_H_
#define RACEVIDEO_FFMPEG_VIDEO_PROBE_H_

#include <filesystem>
#include <string_view>

#include "absl/status/statusor.h"

namespace racevideo {

struct VideoInfo {
  int width;
  int height;
  double frames_per_second;
  double duration_seconds;
  bool has_audio;
};

absl::StatusOr<VideoInfo> ParseFfprobeOutput(std::string_view output);
absl::StatusOr<VideoInfo> ProbeVideo(
    const std::filesystem::path& input_path);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_VIDEO_PROBE_H_
