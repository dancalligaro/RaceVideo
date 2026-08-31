#ifndef RACEVIDEO_OVERLAY_OVERLAY_DATA_H_
#define RACEVIDEO_OVERLAY_OVERLAY_DATA_H_

#include <cstddef>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct TrackPoint {
  absl::Duration timestamp;
  double x;
  double y;
};

struct NavigationSample {
  absl::Duration timestamp;
  double speed_meters_per_second;
  double heading_degrees;
};

struct OverlayData {
  // North-up points normalized to [0, 1], with equal scale on both axes.
  std::vector<TrackPoint> track;
  std::vector<NavigationSample> navigation;
};

struct OverlayFrameData {
  double speed_meters_per_second;
  double heading_degrees;
  GForceReading g_force;
  // Points [0, explored_track_point_count) form the explored blue path. The
  // remainder forms the unexplored white path.
  std::size_t explored_track_point_count;
};

absl::StatusOr<OverlayData> BuildOverlayData(const TelemetryData& telemetry);
absl::StatusOr<OverlayFrameData> SampleOverlayFrame(
    const TelemetryData& telemetry, const OverlayData& overlay,
    absl::Duration timestamp);

}  // namespace racevideo

#endif  // RACEVIDEO_OVERLAY_OVERLAY_DATA_H_
