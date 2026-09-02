#include "ffmpeg/video_encoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
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

std::string FfconcatPath(const std::filesystem::path& path) {
  std::string value = PathAsUtf8(std::filesystem::absolute(path));
  std::replace(value.begin(), value.end(), '\\', '/');
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('\'');
  for (char character : value) {
    if (character == '\'') {
      escaped.append("'\\''");
    } else {
      escaped.push_back(character);
    }
  }
  escaped.push_back('\'');
  return escaped;
}

absl::StatusOr<std::filesystem::path> WriteConcatManifest(
    const std::vector<VideoChapter>& chapters) {
  std::error_code error;
  const std::filesystem::path temporary_directory =
      std::filesystem::temp_directory_path(error);
  if (error) {
    return absl::UnknownError(
        absl::StrCat("cannot locate temporary directory: ", error.message()));
  }
  const auto identifier =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path manifest =
      temporary_directory /
      absl::StrCat("racevideo-chapters-", identifier, ".ffconcat");
  std::ofstream output(manifest, std::ios::binary);
  if (!output) {
    return absl::UnknownError(
        absl::StrCat("cannot create concat manifest: ", manifest.string()));
  }
  output << "ffconcat version 1.0\n";
  for (const VideoChapter& chapter : chapters) {
    output << "file " << FfconcatPath(chapter.path) << '\n'
           << "duration " << Number(chapter.duration_seconds) << '\n';
  }
  output.close();
  if (!output) {
    std::filesystem::remove(manifest, error);
    return absl::UnknownError("cannot write concat manifest");
  }
  return manifest;
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

  int last_percentage = 0;
  std::cout << "Encoding progress: " << std::setw(3) << last_percentage
            << "%\r" << std::flush;
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
    const int percentage = std::min(100, next_to_write * 100 / frame_count);
    if (percentage != last_percentage) {
      last_percentage = percentage;
      std::cout << "Encoding progress: " << std::setw(3) << percentage
                << "%\r" << std::flush;
    }
  }

  {
    std::lock_guard lock(mutex);
    stop = true;
  }
  state_changed.notify_all();
  for (std::thread& thread : workers) thread.join();
  std::cout << '\n';
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
  if (options.chapters.empty()) {
    return absl::InvalidArgumentError("at least one video chapter is required");
  }

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
  if (options.video_pipeline == VideoPipeline::kNvidia) {
    if (options.video_encoder != VideoEncoder::kNvidia) {
      return absl::InvalidArgumentError(
          "the NVIDIA video pipeline requires the NVIDIA encoder");
    }
    if (video.video_codec != "h264" && video.video_codec != "hevc") {
      return absl::FailedPreconditionError(absl::StrCat(
          "the NVIDIA video pipeline supports H.264 and HEVC inputs, not ",
          video.video_codec));
    }
    absl::StatusOr<ProcessResult> filters = RunProcessAndCaptureOutput(
        *ffmpeg, {"-hide_banner", "-filters"});
    if (!filters.ok()) return filters.status();
    if (filters->exit_code != 0 ||
        filters->output.find("scale_cuda") == std::string::npos ||
        filters->output.find("overlay_cuda") == std::string::npos ||
        filters->output.find("hwupload_cuda") == std::string::npos) {
      return absl::FailedPreconditionError(
          "the installed FFmpeg does not provide scale_cuda, overlay_cuda, "
          "and hwupload_cuda");
    }
  }

  const std::string dimensions =
      absl::StrCat(output_dimensions->width, "x", output_dimensions->height);
  const bool scale_output = output_dimensions->width != video.width ||
                            output_dimensions->height != video.height;
  std::string filter;
  if (options.video_pipeline == VideoPipeline::kNvidia) {
    filter = scale_output
                 ? absl::StrCat(
                       "[0:v:0]scale_cuda=", output_dimensions->width, ":",
                       output_dimensions->height,
                       ":format=yuv420p[base];"
                       "[1:v:0]format=yuva420p,hwupload_cuda[over];"
                       "[base][over]overlay_cuda=0:0[v]")
                 : absl::StrCat(
                       "[0:v:0]scale_cuda=", output_dimensions->width, ":",
                       output_dimensions->height,
                       ":format=yuv420p[base];"
                       "[1:v:0]format=yuva420p,hwupload_cuda[over];"
                       "[base][over]overlay_cuda=0:0[v]");
  } else {
    filter = scale_output
                 ? absl::StrCat("[0:v:0]scale=", output_dimensions->width,
                                ":", output_dimensions->height,
                                ":flags=fast_bilinear[base];"
                                "[base][1:v:0]overlay=0:0:format=auto[v]")
                 : "[0:v:0][1:v:0]overlay=0:0:format=auto[v]";
  }
  std::filesystem::path concat_manifest;
  std::vector<std::string> arguments = {
      "-hide_banner", "-loglevel", "error", "-nostdin"};
  if (options.video_pipeline == VideoPipeline::kNvidia) {
    arguments.insert(arguments.end(),
                     {"-init_hw_device", "cuda=cuda:0", "-filter_hw_device",
                      "cuda", "-hwaccel", "cuda", "-hwaccel_output_format",
                      "cuda"});
  }
  arguments.insert(arguments.end(),
                   {"-ss", Number(options.start_seconds), "-t",
                    Number(options.duration_seconds)});
  if (options.chapters.size() == 1) {
    arguments.insert(arguments.end(),
                     {"-i", PathAsUtf8(options.chapters.front().path)});
  } else {
    absl::StatusOr<std::filesystem::path> manifest =
        WriteConcatManifest(options.chapters);
    if (!manifest.ok()) return manifest.status();
    concat_manifest = *manifest;
    arguments.insert(arguments.end(),
                     {"-f", "concat", "-safe", "0", "-i",
                      PathAsUtf8(concat_manifest)});
  }
  arguments.insert(arguments.end(), {
      "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", dimensions, "-framerate",
      Number(video.frames_per_second), "-i", "pipe:0", "-filter_complex",
      filter, "-map", "[v]", "-map", "0:a?"});
  if (options.video_encoder == VideoEncoder::kNvidia) {
    arguments.insert(arguments.end(),
                     {"-c:v", "h264_nvenc", "-preset", "p4", "-rc", "vbr",
                      "-cq", "19", "-b:v", "0"});
  } else {
    arguments.insert(arguments.end(),
                     {"-c:v", "libx264", "-preset", "medium", "-crf", "18"});
  }
  if (options.video_pipeline == VideoPipeline::kSoftware) {
    arguments.insert(arguments.end(), {"-pix_fmt", "yuv420p"});
  } else {
    // CUDA frames are allocated on 32-pixel boundaries. overlay_cuda exposes
    // that allocation size to NVENC, so restore the requested display size in
    // the H.264 cropping metadata.
    const int crop_right =
        (32 - output_dimensions->width % 32) % 32;
    const int crop_bottom =
        (32 - output_dimensions->height % 32) % 32;
    arguments.insert(
        arguments.end(),
        {"-bsf:v", absl::StrCat("h264_metadata=crop_right=", crop_right,
                                ":crop_bottom=", crop_bottom)});
  }
  arguments.insert(arguments.end(),
                   {"-c:a", "copy", "-t", Number(options.duration_seconds),
                    "-movflags", "+faststart", "-n",
                    PathAsUtf8(options.output_path)});

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
  if (!concat_manifest.empty()) {
    std::error_code remove_error;
    std::filesystem::remove(concat_manifest, remove_error);
  }
  if (!process.ok()) return process.status();
  if (process->exit_code != 0) {
    return absl::UnknownError(absl::StrCat(
        "ffmpeg failed with exit code ", process->exit_code,
        process->output.empty() ? "" : ": ", process->output));
  }
  return absl::OkStatus();
}

}  // namespace racevideo
