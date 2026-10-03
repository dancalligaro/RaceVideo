#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "absl/time/time.h"
#include "ffmpeg/video_encoder.h"

int main(int argc, char* argv[]) {
  if (argc != 6) {
    std::cerr << "Usage: racevideo_overlay_benchmark WIDTH HEIGHT FRAMES "
                 "WORKERS cached|reference\n"
                 "Synthetic 30 fps render-only workload; no FFmpeg, disk or "
                 "pipe I/O.\n";
    return 1;
  }
  int values[4]{};
  for (int i = 0; i < 4; ++i) {
    const std::string_view argument(argv[i + 1]);
    const auto parsed = std::from_chars(
        argument.data(), argument.data() + argument.size(), values[i]);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != argument.data() + argument.size())
      return 1;
  }
  const auto [width, height, frames, workers] = values;
  const std::string_view mode(argv[5]);
  if ((mode != "cached" && mode != "reference") || width < 160 ||
      width > 7680 || height < 90 || height > 4320 || frames < 1 ||
      frames > 100000 || workers < 0 || workers > 12) {
    std::cerr << "Invalid benchmark arguments\n";
    return 1;
  }
  racevideo::TelemetryData telemetry;
  racevideo::OverlayData overlay;
  // GPS at 10 Hz, filtered G-force at 30 Hz. Include changing and repeated
  // displayed speeds, moving indicators, and a looping route.
  for (int i = 0; i <= frames; ++i) {
    const double seconds = i / 30.0;
    telemetry.filtered_g_force.push_back(
        {.timestamp = absl::Seconds(seconds),
         .value = {.lateral_g = 0.8 * std::sin(seconds),
                   .longitudinal_g = 0.5 * std::cos(seconds * 0.7)}});
    if (i % 3 == 0 || i == frames) {
      overlay.track.push_back({.timestamp = absl::Seconds(seconds),
                               .x = 0.5 + 0.35 * std::cos(seconds * 0.2),
                               .y = 0.5 + 0.35 * std::sin(seconds * 0.2)});
      overlay.navigation.push_back(
          {.timestamp = absl::Seconds(seconds),
           .speed_meters_per_second = 30 + 15 * std::sin(seconds * 0.3),
           .heading_degrees = std::fmod(seconds * 20, 360.0)});
    }
  }
  racevideo::VideoInfo video{.width = width,
                             .height = height,
                             .frames_per_second = 30,
                             .duration_seconds = frames / 30.0};
  racevideo::VideoEncodeOptions options{};
  options.speed_units = {racevideo::SpeedUnit::kKilometersPerHour,
                         racevideo::SpeedUnit::kMilesPerHour};
  options.overlay_workers = workers;
  std::uint64_t checksum = 0;
  const auto start = std::chrono::steady_clock::now();
  const auto status = racevideo::RenderOverlayFrames(
      telemetry, overlay, video, options, frames,
      [&](std::span<const std::uint8_t> bytes) {
        // Touch each page without adding a full-frame checksum bottleneck.
        for (std::size_t offset = 0; offset < bytes.size(); offset += 4096)
          checksum += bytes[offset];
        return absl::OkStatus();
      },
      mode == "cached");
  if (!status.ok()) {
    std::cerr << status << '\n';
    return 1;
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  std::cout << std::setprecision(6) << "Benchmark: " << mode << ' ' << width
            << 'x' << height << ", " << frames << " frames, elapsed " << seconds
            << " s, " << frames / seconds << " fps, sampled checksum "
            << checksum << '\n';
}
