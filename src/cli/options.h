#ifndef RACEVIDEO_CLI_OPTIONS_H_
#define RACEVIDEO_CLI_OPTIONS_H_

#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "overlay/display_options.h"

namespace racevideo {

struct Options {
  std::filesystem::path input_path;
  bool inspect = false;
  bool inspect_video = false;
  std::filesystem::path extract_gpmf_path;
  std::filesystem::path export_json_path;
  std::filesystem::path export_telemetry_path;
  std::filesystem::path inspect_telemetry_path;
  std::filesystem::path render_frames_path;
  std::filesystem::path output_video_path;
  std::string imu_axis_order;
  double start_seconds = 0.0;
  double duration_seconds = 0.0;
  double render_fps = 30.0;
  int render_width = 1920;
  int render_height = 1080;
  int output_width = 0;
  VideoEncoder video_encoder = VideoEncoder::kSoftware;
  std::vector<SpeedUnit> speed_units;
};

absl::StatusOr<Options> ParseOptions(int argc, char* argv[]);
absl::StatusOr<std::vector<SpeedUnit>> ParseSpeedUnits(std::string value);
absl::StatusOr<VideoEncoder> ParseVideoEncoder(std::string value);

}  // namespace racevideo

#endif  // RACEVIDEO_CLI_OPTIONS_H_
