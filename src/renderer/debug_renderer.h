#ifndef RACEVIDEO_RENDERER_DEBUG_RENDERER_H_
#define RACEVIDEO_RENDERER_DEBUG_RENDERER_H_

#include <cstdint>
#include <filesystem>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "overlay/display_options.h"
#include "overlay/overlay_data.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct DebugRenderOptions {
  std::filesystem::path output_directory;
  double start_seconds;
  double duration_seconds;
  double frames_per_second;
  int width;
  int height;
  std::vector<SpeedUnit> speed_units;
};

absl::Status RenderDebugFrames(const TelemetryData& telemetry,
                               const OverlayData& overlay,
                               const DebugRenderOptions& options);

absl::StatusOr<std::vector<std::uint8_t>> RenderOverlayFrameRgba(
    const TelemetryData& telemetry, const OverlayData& overlay,
    double timestamp_seconds, int width, int height,
    const std::vector<SpeedUnit>& speed_units);

}  // namespace racevideo

#endif  // RACEVIDEO_RENDERER_DEBUG_RENDERER_H_
