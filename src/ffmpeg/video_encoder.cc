#include "ffmpeg/video_encoder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
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
#ifdef _WIN32
  std::replace(value.begin(), value.end(), '\\', '/');
#endif
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
  std::unique_ptr<CachedOverlayRenderer> renderer;
  std::vector<std::uint8_t> reference_pixels;
};

absl::Status ProduceOverlayFrames(const TelemetryData& telemetry,
                                  const OverlayData& overlay,
                                  const VideoInfo& video,
                                  const VideoEncodeOptions& options,
                                  int frame_count, const ByteSink& sink,
                                  bool use_cache) {
  const auto track_setup_start = std::chrono::steady_clock::now();
  absl::StatusOr<TrackRenderState> track_state =
      CreateTrackRenderState(overlay, video.width, video.height);
  if (!track_state.ok()) return track_state.status();
  const double track_setup_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - track_setup_start).count();
  std::atomic<std::int64_t> track_update_nanoseconds{0};
  std::atomic<std::int64_t> sample_nanoseconds{0};
  std::atomic<std::int64_t> frame_render_nanoseconds{0};
  std::atomic<std::int64_t> buffer_nanoseconds{0};
  std::atomic<std::int64_t> track_copy_nanoseconds{0};
  std::atomic<std::int64_t> widget_nanoseconds{0};
  std::int64_t pipe_write_nanoseconds = 0;
  const unsigned available_threads = std::thread::hardware_concurrency();
  const unsigned requested_workers =
      options.overlay_workers > 0
          ? static_cast<unsigned>(std::min(12, options.overlay_workers))
          : std::max(1u, std::min(12u, available_threads == 0
                                           ? 4u
                                           : available_threads / 2));
  constexpr std::size_t kFrameMemoryBudget = 256u * 1024u * 1024u;
  const std::size_t frame_bytes =
      static_cast<std::size_t>(video.width) * video.height * 4;
  // Reserve another frame's worth per worker for track snapshots and widget
  // caches. Always permit one worker for large supported resolutions.
  const int memory_limited_workers = static_cast<int>(
      std::max<std::size_t>(1, kFrameMemoryBudget / (frame_bytes * 2)));
  const int worker_count =
      std::max(1, std::min({frame_count, static_cast<int>(requested_workers),
                            memory_limited_workers}));
  const int capacity = worker_count;
  std::vector<RenderedFrameSlot> slots(static_cast<std::size_t>(capacity));
  for (auto& slot : slots) {
    slot.renderer = std::make_unique<CachedOverlayRenderer>(
        video.width, video.height, options.speed_units);
  }
  std::cout << "Overlay workers: " << worker_count
            << ", RGBA bytes/frame: " << frame_bytes << ", planned pipe GiB: "
            << static_cast<double>(frame_bytes) * frame_count /
                   (1024.0 * 1024 * 1024)
            << '\n';
  std::mutex mutex;
  std::condition_variable state_changed;
  int next_to_assign = 0;
  int next_to_write = 0;
  bool stop = false;

  auto worker = [&]() {
    for (;;) {
      int frame_index = 0;
      OverlayFrameData frame_data{};
      TrackFrameSnapshot track_snapshot{};
      absl::Status preparation_status = absl::OkStatus();
      {
        std::unique_lock lock(mutex);
        state_changed.wait(lock, [&] {
          return stop || next_to_assign >= frame_count ||
                 next_to_assign < next_to_write + capacity;
        });
        if (stop || next_to_assign >= frame_count) return;
        frame_index = next_to_assign++;
        const auto track_update_start = std::chrono::steady_clock::now();
        const double timestamp =
            options.start_seconds + frame_index / video.frames_per_second;
        absl::StatusOr<OverlayFrameData> sampled = SampleOverlayFrame(
            telemetry, overlay, absl::Seconds(timestamp));
        const auto sampled_at = std::chrono::steady_clock::now();
        sample_nanoseconds.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                sampled_at - track_update_start)
                .count(),
            std::memory_order_relaxed);
        if (!sampled.ok()) {
          preparation_status = sampled.status();
        } else {
          frame_data = *sampled;
          absl::StatusOr<TrackFrameSnapshot> snapshot = AdvanceTrackRenderState(
              frame_data.explored_track_point_count, &*track_state, use_cache);
          if (!snapshot.ok()) {
            preparation_status = snapshot.status();
          } else {
            track_snapshot = std::move(*snapshot);
          }
        }
        track_update_nanoseconds.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - sampled_at)
                .count(),
            std::memory_order_relaxed);
      }

      absl::Status render_status = preparation_status;
      if (preparation_status.ok()) {
        const auto frame_render_start = std::chrono::steady_clock::now();
        auto& slot = slots[static_cast<std::size_t>(frame_index % capacity)];
        auto& renderer = *slot.renderer;
        if (use_cache) {
          render_status = renderer.Render(frame_data, track_snapshot);
        } else {
          auto rendered =
              RenderOverlayFrameRgba(frame_data, track_snapshot, video.width,
                                     video.height, options.speed_units);
          render_status = rendered.status();
          if (rendered.ok()) slot.reference_pixels = std::move(*rendered);
        }
        const auto& timing = renderer.timing();
        buffer_nanoseconds.fetch_add(
            static_cast<std::int64_t>(timing.buffer_seconds * 1e9));
        track_copy_nanoseconds.fetch_add(
            static_cast<std::int64_t>(timing.track_seconds * 1e9));
        widget_nanoseconds.fetch_add(
            static_cast<std::int64_t>(timing.widget_seconds * 1e9));
        frame_render_nanoseconds.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - frame_render_start)
                .count(),
            std::memory_order_relaxed);
      }

      {
        std::lock_guard lock(mutex);
        RenderedFrameSlot& slot =
            slots[static_cast<std::size_t>(frame_index % capacity)];
        slot.status = render_status;
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

  auto last_progress_report = std::chrono::steady_clock::now();
  std::cout << "Encoding progress:   0%\r" << std::flush;
  absl::Status result = absl::OkStatus();
  while (next_to_write < frame_count) {
    std::span<const std::uint8_t> pixels;
    {
      std::unique_lock lock(mutex);
      RenderedFrameSlot& slot =
          slots[static_cast<std::size_t>(next_to_write % capacity)];
      state_changed.wait(lock, [&] { return slot.ready; });
      if (!slot.status.ok()) {
        result = slot.status;
        stop = true;
      } else {
        pixels = use_cache
                     ? slot.renderer->pixels()
                     : std::span<const std::uint8_t>(slot.reference_pixels);
      }
    }
    state_changed.notify_all();
    if (!result.ok()) break;
    const auto pipe_write_start = std::chrono::steady_clock::now();
    result = sink(std::span<const std::uint8_t>(pixels));
    pipe_write_nanoseconds +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - pipe_write_start)
            .count();
    if (!result.ok()) {
      {
        std::lock_guard lock(mutex);
        stop = true;
      }
      state_changed.notify_all();
      break;
    }
    // The writer owns this slot until the synchronous pipe write completes.
    // Only then may a worker render another frame into its persistent buffer.
    {
      std::lock_guard lock(mutex);
      slots[static_cast<std::size_t>(next_to_write % capacity)].ready = false;
      ++next_to_write;
    }
    state_changed.notify_all();
    const auto now = std::chrono::steady_clock::now();
    if (next_to_write == frame_count ||
        now - last_progress_report >= std::chrono::seconds(1)) {
      last_progress_report = now;
      const int percentage =
          std::min(100, next_to_write * 100 / frame_count);
      std::cout << "Encoding progress: " << std::setw(3) << percentage
                << "% | render worker time " << std::fixed
                << std::setprecision(1)
                << frame_render_nanoseconds.load(std::memory_order_relaxed) /
                       1e9
                << " s | track "
                << track_update_nanoseconds.load(std::memory_order_relaxed) /
                       1e9
                << " s | FFmpeg wait " << pipe_write_nanoseconds / 1e9 << " s\r"
                << std::defaultfloat << std::flush;
    }
  }

  {
    std::lock_guard lock(mutex);
    stop = true;
  }
  state_changed.notify_all();
  for (std::thread& thread : workers) thread.join();
  std::cout << "\nOverlay timing: track setup " << std::fixed
            << std::setprecision(2) << track_setup_seconds
            << " s, incremental track "
            << track_update_nanoseconds.load(std::memory_order_relaxed) / 1e9
            << " s, frame rasterization "
            << frame_render_nanoseconds.load(std::memory_order_relaxed) / 1e9
            << " cumulative worker-s, FFmpeg pipe/wait "
            << pipe_write_nanoseconds / 1e9 << " s.\n"
            << std::defaultfloat;
  if (use_cache)
    std::cout << "Render breakdown (summed worker seconds): buffer/setup "
              << buffer_nanoseconds.load() / 1e9 << ", track copy/arrow "
              << track_copy_nanoseconds.load() / 1e9 << ", widgets "
              << widget_nanoseconds.load() / 1e9 << '\n';
  std::cout << "Telemetry sampling: " << sample_nanoseconds.load() / 1e9
            << " s.\n";
  std::cout << std::setprecision(6);
  return result;
}

}  // namespace

absl::Status RenderOverlayFrames(const TelemetryData& telemetry,
                                 const OverlayData& overlay,
                                 const VideoInfo& video,
                                 const VideoEncodeOptions& options,
                                 int frame_count, const ByteSink& sink,
                                 bool use_cache) {
  if (frame_count < 1 || video.width < 160 || video.width > 7680 ||
      video.height < 90 || video.height > 4320 ||
      !std::isfinite(video.frames_per_second) || video.frames_per_second <= 0 ||
      !std::isfinite(options.start_seconds) || options.overlay_workers < 0 ||
      options.overlay_workers > 12) {
    return absl::InvalidArgumentError("invalid overlay stream configuration");
  }
  return ProduceOverlayFrames(telemetry, overlay, video, options, frame_count,
                              sink, use_cache);
}

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
  if (options.video_encoder == VideoEncoder::kVaapi) {
#ifndef __linux__
    return absl::FailedPreconditionError("VA-API encoding requires Linux");
#endif
    if (!options.vaapi_device.is_absolute()) {
      return absl::InvalidArgumentError(
          "--vaapi_device must be an absolute DRM render device path");
    }
    if (options.video_pipeline != VideoPipeline::kSoftware) {
      return absl::InvalidArgumentError(
          "VA-API encoding requires --video_pipeline=software");
    }
  }
  if (options.video_encoder != VideoEncoder::kSoftware) {
    std::string_view encoder_name = "h264_videotoolbox";
    if (options.video_encoder == VideoEncoder::kNvidia) {
      encoder_name = "h264_nvenc";
    } else if (options.video_encoder == VideoEncoder::kVaapi) {
      encoder_name = "h264_vaapi";
    }
    absl::StatusOr<ProcessResult> encoders = RunProcessAndCaptureOutput(
        *ffmpeg, {"-hide_banner", "-encoders"});
    if (!encoders.ok()) return encoders.status();
    if (encoders->exit_code != 0 ||
        encoders->output.find(encoder_name) == std::string::npos) {
      return absl::FailedPreconditionError(
          absl::StrCat("the installed FFmpeg does not provide the ",
                       encoder_name, " encoder"));
    }
  }
  if (options.video_encoder == VideoEncoder::kVaapi) {
    const auto filters =
        RunProcessAndCaptureOutput(*ffmpeg, {"-hide_banner", "-filters"});
    if (!filters.ok()) return filters.status();
    if (filters->exit_code != 0 ||
        filters->output.find(" hwupload ") == std::string::npos) {
      return absl::FailedPreconditionError(
          "the installed FFmpeg does not provide the hwupload filter");
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
  if (options.video_encoder == VideoEncoder::kVaapi) {
    // Composite in software, then upload NV12 frames to the VA-API encoder.
    filter.replace(filter.size() - 3, 3, "[composited]");
    filter += ";[composited]format=nv12,hwupload[v]";
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
  if (options.video_encoder == VideoEncoder::kVaapi) {
    arguments.insert(arguments.end(),
                     {"-vaapi_device", PathAsUtf8(options.vaapi_device)});
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
  } else if (options.video_encoder == VideoEncoder::kVaapi) {
    arguments.insert(arguments.end(),
                     {"-c:v", "h264_vaapi", "-profile:v", "high", "-qp", "19"});
  } else if (options.video_encoder == VideoEncoder::kVideoToolbox) {
    arguments.insert(arguments.end(),
                     {"-c:v", "h264_videotoolbox", "-profile:v", "high",
                      "-q:v", "65"});
  } else {
    arguments.insert(arguments.end(),
                     {"-c:v", "libx264", "-preset", "medium", "-crf", "18"});
  }
  if (options.video_pipeline == VideoPipeline::kSoftware &&
      options.video_encoder != VideoEncoder::kVaapi) {
    arguments.insert(arguments.end(), {"-pix_fmt", "yuv420p"});
  } else if (options.video_pipeline == VideoPipeline::kNvidia) {
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
                    "-n", PathAsUtf8(options.output_path)});

  VideoInfo output_video = video;
  output_video.width = output_dimensions->width;
  output_video.height = output_dimensions->height;

  const int frame_count = static_cast<int>(
      std::ceil(options.duration_seconds * video.frames_per_second));
  const auto submission_start = std::chrono::steady_clock::now();
  auto submission_end = submission_start;
  const InputProducer producer = [&](const ByteSink& sink) -> absl::Status {
    const absl::Status status = RenderOverlayFrames(
        telemetry, overlay, output_video, options, frame_count, sink);
    submission_end = std::chrono::steady_clock::now();
    if (status.ok()) std::cout << "Finalizing video...\n" << std::flush;
    return status;
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
  std::cout << "Frame submission elapsed: "
            << std::chrono::duration<double>(submission_end - submission_start)
                   .count()
            << " s; FFmpeg finalization: "
            << std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             submission_end)
                   .count()
            << " s.\n";
  return absl::OkStatus();
}

}  // namespace racevideo
