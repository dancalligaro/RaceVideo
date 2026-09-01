#include "ffmpeg/video_encoder.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
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

struct RenderedFrameSlot {
  bool ready = false;
  absl::Status status = absl::OkStatus();
  std::vector<std::uint8_t> pixels;
};

absl::Status ProduceOverlayFrames(const TelemetryData& telemetry,
                                  const OverlayData& overlay,
                                  const VideoInfo& video,
                                  const VideoEncodeOptions& options,
                                  int frame_count, const ByteSink& sink) {
  const unsigned available_threads = std::thread::hardware_concurrency();
  const unsigned requested_workers = std::max(
      1u, std::min(12u, available_threads == 0 ? 4u : available_threads / 2));
  constexpr std::size_t kFrameMemoryBudget = 256u * 1024u * 1024u;
  const std::size_t frame_bytes =
      static_cast<std::size_t>(video.width) * video.height * 4;
  const int memory_limited_workers = static_cast<int>(
      std::max<std::size_t>(1, kFrameMemoryBudget / frame_bytes));
  const int worker_count =
      std::max(1, std::min({frame_count, static_cast<int>(requested_workers),
                            memory_limited_workers}));
  const int capacity = worker_count;
  std::vector<RenderedFrameSlot> slots(static_cast<std::size_t>(capacity));
  std::mutex mutex;
  std::condition_variable state_changed;
  int next_to_assign = 0;
  int next_to_write = 0;
  bool stop = false;

  auto worker = [&]() {
    for (;;) {
      int frame_index = 0;
      {
        std::unique_lock lock(mutex);
        state_changed.wait(lock, [&] {
          return stop || next_to_assign >= frame_count ||
                 next_to_assign < next_to_write + capacity;
        });
        if (stop || next_to_assign >= frame_count) return;
        frame_index = next_to_assign++;
      }

      const double timestamp =
          options.start_seconds + frame_index / video.frames_per_second;
      absl::StatusOr<std::vector<std::uint8_t>> pixels =
          RenderOverlayFrameRgba(telemetry, overlay, timestamp, video.width,
                                 video.height, options.speed_units);

      {
        std::lock_guard lock(mutex);
        RenderedFrameSlot& slot =
            slots[static_cast<std::size_t>(frame_index % capacity)];
        slot.status = pixels.status();
        if (pixels.ok()) slot.pixels = std::move(*pixels);
        slot.ready = true;
      }
      state_changed.notify_all();
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int index = 0; index < worker_count; ++index) {
    workers.emplace_back(worker);
  }

  absl::Status result = absl::OkStatus();
  while (next_to_write < frame_count) {
    std::vector<std::uint8_t> pixels;
    {
      std::unique_lock lock(mutex);
      RenderedFrameSlot& slot =
          slots[static_cast<std::size_t>(next_to_write % capacity)];
      state_changed.wait(lock, [&] { return slot.ready; });
      if (!slot.status.ok()) {
        result = slot.status;
        stop = true;
      } else {
        pixels = std::move(slot.pixels);
        slot.ready = false;
        ++next_to_write;
      }
    }
    state_changed.notify_all();
    if (!result.ok()) break;
    result = sink(std::span<const std::uint8_t>(pixels));
    if (!result.ok()) {
      {
        std::lock_guard lock(mutex);
        stop = true;
      }
      state_changed.notify_all();
      break;
    }
  }

  {
    std::lock_guard lock(mutex);
    stop = true;
  }
  state_changed.notify_all();
  for (std::thread& thread : workers) thread.join();
  return result;
}

}  // namespace

absl::StatusOr<VideoDimensions> DetermineOutputDimensions(
    const VideoInfo& video, int output_width) {
  if (video.width <= 0 || video.height <= 0) {
    return absl::InvalidArgumentError("source video dimensions are invalid");
  }
  if (output_width == 0) return VideoDimensions{video.width, video.height};
  if (output_width < 160 || output_width > video.width ||
      output_width % 2 != 0) {
    return absl::InvalidArgumentError(
        "output width must be even, at least 160, and no larger than the "
        "source width");
  }
  const int output_height = static_cast<int>(std::lround(
      static_cast<double>(video.height) * output_width / video.width / 2.0)) *
                            2;
  if (output_height < 90 || output_height > 4320) {
    return absl::InvalidArgumentError(
        "scaled output height is outside the supported range [90, 4320]");
  }
  return VideoDimensions{output_width, output_height};
}

absl::Status EncodeOverlayVideo(const TelemetryData& telemetry,
                                const OverlayData& overlay,
                                const VideoInfo& video,
                                const VideoEncodeOptions& options) {
  absl::StatusOr<std::filesystem::path> ffmpeg =
      FindExecutableOnPath("ffmpeg");
  if (!ffmpeg.ok()) return ffmpeg.status();

  absl::StatusOr<VideoDimensions> output_dimensions =
      DetermineOutputDimensions(video, options.output_width);
  if (!output_dimensions.ok()) return output_dimensions.status();
  if (options.video_encoder == VideoEncoder::kNvidia) {
    absl::StatusOr<ProcessResult> encoders = RunProcessAndCaptureOutput(
        *ffmpeg, {"-hide_banner", "-encoders"});
    if (!encoders.ok()) return encoders.status();
    if (encoders->exit_code != 0 ||
        encoders->output.find("h264_nvenc") == std::string::npos) {
      return absl::FailedPreconditionError(
          "the installed FFmpeg does not provide the h264_nvenc encoder");
    }
  }

  const std::string dimensions =
      absl::StrCat(output_dimensions->width, "x", output_dimensions->height);
  const bool scale_output = output_dimensions->width != video.width ||
                            output_dimensions->height != video.height;
  const std::string filter =
      scale_output
          ? absl::StrCat("[0:v:0]scale=", output_dimensions->width, ":",
                         output_dimensions->height,
                         ":flags=fast_bilinear[base];"
                         "[base][1:v:0]overlay=0:0:format=auto[v]")
          : "[0:v:0][1:v:0]overlay=0:0:format=auto[v]";
  std::vector<std::string> arguments = {
      "-hide_banner", "-loglevel", "error", "-nostdin", "-ss",
      Number(options.start_seconds), "-t", Number(options.duration_seconds),
      "-i", PathAsUtf8(options.input_path), "-f", "rawvideo", "-pixel_format",
      "rgba", "-video_size", dimensions, "-framerate",
      Number(video.frames_per_second), "-i", "pipe:0", "-filter_complex",
      filter, "-map", "[v]", "-map", "0:a?"};
  if (options.video_encoder == VideoEncoder::kNvidia) {
    arguments.insert(arguments.end(),
                     {"-c:v", "h264_nvenc", "-preset", "p4", "-rc", "vbr",
                      "-cq", "19", "-b:v", "0"});
  } else {
    arguments.insert(arguments.end(),
                     {"-c:v", "libx264", "-preset", "medium", "-crf", "18"});
  }
  arguments.insert(arguments.end(),
                   {"-pix_fmt", "yuv420p", "-c:a", "copy", "-t",
                    Number(options.duration_seconds), "-movflags",
                    "+faststart", "-n", PathAsUtf8(options.output_path)});

  VideoInfo output_video = video;
  output_video.width = output_dimensions->width;
  output_video.height = output_dimensions->height;

  const int frame_count = static_cast<int>(
      std::ceil(options.duration_seconds * video.frames_per_second));
  const InputProducer producer = [&](const ByteSink& sink) -> absl::Status {
    return ProduceOverlayFrames(telemetry, overlay, output_video, options,
                                frame_count, sink);
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
