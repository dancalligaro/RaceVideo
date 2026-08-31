#ifndef RACEVIDEO_TELEMETRY_G_FORCE_H_
#define RACEVIDEO_TELEMETRY_G_FORCE_H_

#include "absl/status/status.h"
#include "telemetry/telemetry.h"

namespace racevideo {

inline constexpr double kStandardGravityMetersPerSecondSquared = 9.80665;
inline constexpr double kDefaultGForceFilterCutoffHz = 5.0;

struct GForceStatistics {
  double peak_forward_g = 0.0;
  double peak_braking_g = 0.0;
  double peak_left_g = 0.0;
  double peak_right_g = 0.0;
  double peak_vertical_dynamic_g = 0.0;
};

absl::Status GenerateFilteredGForce(
    double cutoff_hz, TelemetryData* telemetry);
GForceStatistics ComputeGForceStatistics(const TelemetryData& telemetry);

}  // namespace racevideo

#endif  // RACEVIDEO_TELEMETRY_G_FORCE_H_
