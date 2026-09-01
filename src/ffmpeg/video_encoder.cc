#include "ffmpeg/video_encoder.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "ffmpeg/process.h"
#include "renderer/debug_renderer.h"

namespace racevideo {
namespace {

std::string PathAsUtf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string Number(double value) {
  return absl::StrCat(value);
}

}  // namespace

absl::Status EncodeOverlayVideo(const TelemetryData& telemetry,
                                const OverlayData& overlay,
                                const VideoInfo& video,
                                const VideoEncodeOptions& options) {
  absl::StatusOr<std::filesystem::path> ffmpeg =
      FindExecutableOnPath("ffmpeg");
  if (!ffmpeg.ok()) return ffmpeg.status();

  const std::string dimensions =
      absl::StrCat(video.width, "x", video.height);
  std::vector<std::string> arguments = {
      "-hide_banner", "-loglevel", "error", "-nostdin", "-ss",
      Number(options.start_seconds), "-t", Number(options.duration_seconds),
      "-i", PathAsUtf8(options.input_path), "-f", "rawvideo", "-pixel_format",
      "rgba", "-video_size", dimensions, "-framerate",
      Number(video.frames_per_second), "-i", "pipe:0", "-filter_complex",
      "[0:v:0][1:v:0]overlay=0:0:format=auto[v]", "-map", "[v]", "-map",
      "0:a?", "-c:v", "libx264", "-preset", "medium", "-crf", "18",
      "-pix_fmt", "yuv420p", "-c:a", "copy", "-t",
      Number(options.duration_seconds), "-movflags", "+faststart", "-n",
      PathAsUtf8(options.output_path)};

  const int frame_count = static_cast<int>(
      std::ceil(options.duration_seconds * video.frames_per_second));
  const InputProducer producer = [&](const ByteSink& sink) -> absl::Status {
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
      const double timestamp =
          options.start_seconds + frame_index / video.frames_per_second;
      absl::StatusOr<std::vector<std::uint8_t>> pixels =
          RenderOverlayFrameRgba(telemetry, overlay, timestamp, video.width,
                                 video.height, options.speed_unit);
      if (!pixels.ok()) return pixels.status();
      const absl::Status status = sink(std::span<const std::uint8_t>(*pixels));
      if (!status.ok()) return status;
    }
    return absl::OkStatus();
  };
  absl::StatusOr<ProcessResult> process =
      RunProcessWithInput(*ffmpeg, arguments, producer);
  if (!process.ok()) return process.status();
  if (process->exit_code != 0) {
    return absl::UnknownError(absl::StrCat(
        "ffmpeg failed with exit code ", process->exit_code,
        process->output.empty() ? "" : ": ", process->output));
  }
  return absl::OkStatus();
}

}  // namespace racevideo
