#ifndef RACEVIDEO_FFMPEG_VIDEO_PROBE_H_
#define RACEVIDEO_FFMPEG_VIDEO_PROBE_H_

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace racevideo {

struct VideoInfo {
  int width;
  int height;
  double frames_per_second;
  double duration_seconds;
  bool has_audio;
  std::string video_codec;
  std::string pixel_format;
  std::string video_time_base;
  std::string rotation;
  std::string color_space;
  std::string color_transfer;
  std::string color_primaries;
  std::string audio_codec;
  std::string audio_sample_rate;
  int audio_channels = 0;
  std::string audio_channel_layout;
  std::string audio_time_base;
};

absl::StatusOr<VideoInfo> ParseFfprobeOutput(std::string_view output);
absl::StatusOr<VideoInfo> ProbeVideo(
    const std::filesystem::path& input_path);
absl::Status ValidateCompatibleVideo(const VideoInfo& expected,
                                     const VideoInfo& candidate);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_VIDEO_PROBE_H_
