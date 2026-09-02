#ifndef RACEVIDEO_TELEMETRY_MOUNT_ORIENTATION_H_
#define RACEVIDEO_TELEMETRY_MOUNT_ORIENTATION_H_

#include <cstddef>
#include <string_view>

#include "absl/status/status.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct FineMountCalibration {
  bool applied = false;
  double pitch_degrees = 0.0;
  double roll_degrees = 0.0;
  std::size_t stationary_samples = 0;
};

absl::Status DetectAndApplyMountOrientation(TelemetryData* telemetry);
FineMountCalibration ApplyStationaryMountCalibration(TelemetryData* telemetry);
std::string_view MountOrientationName(MountOrientation orientation);

}  // namespace racevideo

#endif  // RACEVIDEO_TELEMETRY_MOUNT_ORIENTATION_H_
