#include "cli/options.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"

ABSL_FLAG(std::string, input, "", "Path to the input GoPro MP4 file");
ABSL_FLAG(bool, inspect, false, "Inspect the embedded GoPro metadata");
ABSL_FLAG(bool, inspect_video, false,
          "Inspect video and audio streams using external ffprobe");
ABSL_FLAG(std::string, extract_gpmf, "",
          "Write the raw GPMF metadata payloads to this file");
ABSL_FLAG(std::string, export_json, "",
          "Decode telemetry and write it as JSON");
ABSL_FLAG(std::string, export_telemetry, "",
          "Decode telemetry and write a compact Protobuf file");
ABSL_FLAG(std::string, inspect_telemetry, "",
          "Validate and summarize a RaceVideo Protobuf file");
ABSL_FLAG(std::string, imu_axis_order, "",
          "Encoded IMU component order, e.g. ZXY, yXZ, or YxZ");
ABSL_FLAG(std::string, render_frames, "",
          "Write transparent debug overlay PNGs to this folder");
ABSL_FLAG(std::string, output_video, "",
          "Render the telemetry overlay into an MP4 using external FFmpeg");
ABSL_FLAG(double, start_seconds, 0.0,
          "First video timestamp to render, in seconds");
ABSL_FLAG(double, duration_seconds, 0.0,
          "Duration to render; zero means the remainder for --output_video");
ABSL_FLAG(double, render_fps, 30.0, "Debug overlay frame rate");
ABSL_FLAG(int, render_width, 1920, "Debug overlay width in pixels");
ABSL_FLAG(int, render_height, 1080, "Debug overlay height in pixels");
ABSL_FLAG(std::string, speed_unit, "",
          "Speed rows to display: kmh, mph, kmh,mph, or mph,kmh; omitted "
          "hides speed");

namespace racevideo {

absl::StatusOr<std::vector<SpeedUnit>> ParseSpeedUnits(std::string value) {
  absl::AsciiStrToLower(&value);
  if (value.empty()) return std::vector<SpeedUnit>{};
  if (value == "kmh") {
    return std::vector<SpeedUnit>{SpeedUnit::kKilometersPerHour};
  }
  if (value == "mph") {
    return std::vector<SpeedUnit>{SpeedUnit::kMilesPerHour};
  }
  if (value == "kmh,mph") {
    return std::vector<SpeedUnit>{SpeedUnit::kKilometersPerHour,
                                  SpeedUnit::kMilesPerHour};
  }
  if (value == "mph,kmh") {
    return std::vector<SpeedUnit>{SpeedUnit::kMilesPerHour,
                                  SpeedUnit::kKilometersPerHour};
  }
  return absl::InvalidArgumentError(
      "--speed_unit must be kmh, mph, kmh,mph, or mph,kmh");
}

absl::StatusOr<Options> ParseOptions(int argc, char* argv[]) {
  std::vector<char*> positional_arguments = absl::ParseCommandLine(argc, argv);
  if (positional_arguments.size() > 1) {
    return absl::InvalidArgumentError(
        "positional arguments are not supported; use --input=<path>");
  }

  const std::string input = absl::GetFlag(FLAGS_input);
  const std::string inspect_telemetry =
      absl::GetFlag(FLAGS_inspect_telemetry);
  if (input.empty() == inspect_telemetry.empty()) {
    return absl::InvalidArgumentError(
        "specify exactly one of --input or --inspect_telemetry");
  }

  const std::string render_frames = absl::GetFlag(FLAGS_render_frames);
  const std::string output_video = absl::GetFlag(FLAGS_output_video);
  const double start_seconds = absl::GetFlag(FLAGS_start_seconds);
  const double duration_seconds = absl::GetFlag(FLAGS_duration_seconds);
  const double render_fps = absl::GetFlag(FLAGS_render_fps);
  const int render_width = absl::GetFlag(FLAGS_render_width);
  const int render_height = absl::GetFlag(FLAGS_render_height);
  absl::StatusOr<std::vector<SpeedUnit>> speed_units =
      ParseSpeedUnits(absl::GetFlag(FLAGS_speed_unit));
  if (!speed_units.ok()) return speed_units.status();
  if (!render_frames.empty()) {
    if (!std::isfinite(start_seconds) || start_seconds < 0.0 ||
        !std::isfinite(duration_seconds) || duration_seconds <= 0.0 ||
        !std::isfinite(render_fps) || render_fps <= 0.0 ||
        render_fps > 120.0) {
      return absl::InvalidArgumentError(
          "render range must have a nonnegative finite start, a positive "
          "finite duration, and an FPS in (0, 120]");
    }
    if (render_width < 160 || render_height < 90 || render_width > 7680 ||
        render_height > 4320) {
      return absl::InvalidArgumentError(
          "render dimensions must be between 160x90 and 7680x4320");
    }
    if (std::ceil(duration_seconds * render_fps) > 10000.0) {
      return absl::InvalidArgumentError(
          "debug render is limited to 10000 frames per invocation");
    }
    if (input.empty()) {
      return absl::InvalidArgumentError(
          "--render_frames requires an MP4 --input");
    }
  }
  if (!output_video.empty()) {
    if (input.empty()) {
      return absl::InvalidArgumentError(
          "--output_video requires an MP4 --input");
    }
    if (!std::isfinite(start_seconds) || start_seconds < 0.0 ||
        !std::isfinite(duration_seconds) || duration_seconds < 0.0) {
      return absl::InvalidArgumentError(
          "video render range requires a nonnegative finite start and "
          "duration");
    }
    if (absl::GetFlag(FLAGS_imu_axis_order).empty()) {
      return absl::InvalidArgumentError(
          "--output_video requires --imu_axis_order so acceleration can be "
          "mapped to vehicle axes");
    }
  }

  return Options{.input_path = input,
                 .inspect = absl::GetFlag(FLAGS_inspect),
                 .inspect_video = absl::GetFlag(FLAGS_inspect_video),
                 .extract_gpmf_path = absl::GetFlag(FLAGS_extract_gpmf),
                 .export_json_path = absl::GetFlag(FLAGS_export_json),
                 .export_telemetry_path =
                     absl::GetFlag(FLAGS_export_telemetry),
                 .inspect_telemetry_path = inspect_telemetry,
                 .render_frames_path = render_frames,
                 .output_video_path = output_video,
                 .imu_axis_order = absl::GetFlag(FLAGS_imu_axis_order),
                 .start_seconds = start_seconds,
                 .duration_seconds = duration_seconds,
                 .render_fps = render_fps,
                 .render_width = render_width,
                 .render_height = render_height,
                 .speed_units = std::move(*speed_units)};
}

}  // namespace racevideo
