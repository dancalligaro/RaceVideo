#include "ffmpeg/video_probe.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "ffmpeg/process.h"

namespace racevideo {
namespace {

std::string Unquote(std::string_view value) {
  value = absl::StripAsciiWhitespace(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value.remove_prefix(1);
    value.remove_suffix(1);
  }
  return std::string(value);
}

absl::StatusOr<double> ParseFrameRate(std::string_view value) {
  const std::vector<std::string_view> parts = absl::StrSplit(value, '/');
  if (parts.size() != 2) {
    return absl::DataLossError("ffprobe returned an invalid frame rate");
  }
  double numerator = 0.0;
  double denominator = 0.0;
  if (!absl::SimpleAtod(parts[0], &numerator) ||
      !absl::SimpleAtod(parts[1], &denominator) || denominator == 0.0) {
    return absl::DataLossError("ffprobe returned an invalid frame rate");
  }
  const double result = numerator / denominator;
  if (!std::isfinite(result) || result <= 0.0 || result > 1000.0) {
    return absl::DataLossError("ffprobe returned an unsupported frame rate");
  }
  return result;
}

std::string PathAsUtf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

}  // namespace

absl::StatusOr<VideoInfo> ParseFfprobeOutput(std::string_view output) {
  int width = 0;
  int height = 0;
  double duration = 0.0;
  std::string average_frame_rate;
  std::string real_frame_rate;
  bool has_video = false;
  bool has_audio = false;
  for (std::string_view line : absl::StrSplit(output, '\n')) {
    line = absl::StripAsciiWhitespace(line);
    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, separator);
    const std::string value = Unquote(line.substr(separator + 1));
    if (key.ends_with(".codec_type")) {
      has_video = has_video || value == "video";
      has_audio = has_audio || value == "audio";
    } else if (key.ends_with(".width") && width == 0) {
      if (!absl::SimpleAtoi(value, &width)) {
        return absl::DataLossError("ffprobe returned an invalid video width");
      }
    } else if (key.ends_with(".height") && height == 0) {
      if (!absl::SimpleAtoi(value, &height)) {
        return absl::DataLossError("ffprobe returned an invalid video height");
      }
    } else if (key.ends_with(".avg_frame_rate") &&
               average_frame_rate.empty()) {
      average_frame_rate = value;
    } else if (key.ends_with(".r_frame_rate") && real_frame_rate.empty()) {
      real_frame_rate = value;
    } else if (key == "format.duration") {
      if (!absl::SimpleAtod(value, &duration)) {
        return absl::DataLossError("ffprobe returned an invalid duration");
      }
    }
  }
  if (!has_video || width <= 0 || width > 32768 || height <= 0 ||
      height > 32768) {
    return absl::DataLossError(
        "ffprobe did not return a supported primary video stream");
  }
  if (!std::isfinite(duration) || duration <= 0.0) {
    return absl::DataLossError("ffprobe did not return a positive duration");
  }
  absl::StatusOr<double> frames_per_second =
      ParseFrameRate(average_frame_rate);
  if (!frames_per_second.ok()) {
    frames_per_second = ParseFrameRate(real_frame_rate);
  }
  if (!frames_per_second.ok()) return frames_per_second.status();
  return VideoInfo{.width = width,
                   .height = height,
                   .frames_per_second = *frames_per_second,
                   .duration_seconds = duration,
                   .has_audio = has_audio};
}

absl::StatusOr<VideoInfo> ProbeVideo(
    const std::filesystem::path& input_path) {
  absl::StatusOr<std::filesystem::path> ffprobe =
      FindExecutableOnPath("ffprobe");
  if (!ffprobe.ok()) return ffprobe.status();
  const std::vector<std::string> arguments = {
      "-hide_banner",
      "-v",
      "error",
      "-show_entries",
      "stream=codec_type,width,height,avg_frame_rate,r_frame_rate:format=duration",
      "-of",
      "flat",
      PathAsUtf8(input_path)};
  absl::StatusOr<ProcessResult> process =
      RunProcessAndCaptureOutput(*ffprobe, arguments);
  if (!process.ok()) return process.status();
  if (process->exit_code != 0) {
    return absl::DataLossError(absl::StrCat(
        "ffprobe failed with exit code ", process->exit_code, ": ",
        process->output));
  }
  return ParseFfprobeOutput(process->output);
}

}  // namespace racevideo
