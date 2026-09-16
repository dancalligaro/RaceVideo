#ifndef RACEVIDEO_RENDERER_DEBUG_RENDERER_H_
#define RACEVIDEO_RENDERER_DEBUG_RENDERER_H_

#include <cstdint>
#include <cstddef>
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

struct TrackPixelPoint {
  int x;
  int y;
};

struct TrackRenderState {
  int frame_x;
  int frame_y;
  int size;
  int blue_thickness;
  std::size_t explored_point_count = 0;
  std::vector<TrackPixelPoint> points;
  std::vector<std::uint8_t> pixels;
};

struct TrackFrameSnapshot {
  int frame_x;
  int frame_y;
  int size;
  int arrow_x;
  int arrow_y;
  bool has_arrow;
  std::vector<std::uint8_t> pixels;
};

absl::StatusOr<TrackRenderState> CreateTrackRenderState(
    const OverlayData& overlay, int frame_width, int frame_height);
absl::StatusOr<TrackFrameSnapshot> AdvanceTrackRenderState(
    std::size_t explored_point_count, TrackRenderState* state);

absl::Status RenderDebugFrames(const TelemetryData& telemetry,
                               const OverlayData& overlay,
                               const DebugRenderOptions& options);

absl::StatusOr<std::vector<std::uint8_t>> RenderOverlayFrameRgba(
    const TelemetryData& telemetry, const OverlayData& overlay,
    double timestamp_seconds, int width, int height,
    const std::vector<SpeedUnit>& speed_units);
absl::StatusOr<std::vector<std::uint8_t>> RenderOverlayFrameRgba(
    const OverlayFrameData& frame, const TrackFrameSnapshot& track,
    int width, int height, const std::vector<SpeedUnit>& speed_units);

}  // namespace racevideo

#endif  // RACEVIDEO_RENDERER_DEBUG_RENDERER_H_
