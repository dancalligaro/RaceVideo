#include "telemetry/g_force.h"

#include <algorithm>
#include <cmath>

#include "absl/status/status.h"
#include "absl/time/time.h"

namespace racevideo {
namespace {

constexpr double kTwoPi = 6.28318530717958647692;

GForceReading ToGForce(const Vector3& acceleration) {
  return {
      .lateral_g =
          acceleration.x / kStandardGravityMetersPerSecondSquared,
      .longitudinal_g =
          acceleration.y / kStandardGravityMetersPerSecondSquared,
      .vertical_dynamic_g =
          acceleration.z / kStandardGravityMetersPerSecondSquared - 1.0};
}

double Filter(double input, double previous, double alpha) {
  return previous + alpha * (input - previous);
}

}  // namespace

absl::Status GenerateFilteredGForce(double cutoff_hz,
                                    TelemetryData* telemetry) {
  if (!telemetry->acceleration_metadata.values_are_forward_upright) {
    return absl::FailedPreconditionError(
        "forward-upright inertial values are required for G-force");
  }
  if (!std::isfinite(cutoff_hz) || cutoff_hz <= 0.0) {
    return absl::InvalidArgumentError(
        "G-force filter cutoff must be positive and finite");
  }
  telemetry->filtered_g_force.clear();
  telemetry->g_force_filter_cutoff_hz = cutoff_hz;
  const auto& acceleration =
      telemetry->acceleration_meters_per_second_squared;
  if (acceleration.empty()) return absl::OkStatus();

  telemetry->filtered_g_force.reserve(acceleration.size());
  GForceReading filtered = ToGForce(acceleration.front().value);
  telemetry->filtered_g_force.push_back(
      {.timestamp = acceleration.front().timestamp, .value = filtered});
  const double time_constant_seconds = 1.0 / (kTwoPi * cutoff_hz);
  for (std::size_t i = 1; i < acceleration.size(); ++i) {
    const double delta_seconds = absl::ToDoubleSeconds(
        acceleration[i].timestamp - acceleration[i - 1].timestamp);
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) {
      return absl::DataLossError(
          "accelerometer timestamps must be strictly increasing");
    }
    const double alpha =
        delta_seconds / (time_constant_seconds + delta_seconds);
    const GForceReading input = ToGForce(acceleration[i].value);
    filtered = {
        .lateral_g = Filter(input.lateral_g, filtered.lateral_g, alpha),
        .longitudinal_g =
            Filter(input.longitudinal_g, filtered.longitudinal_g, alpha),
        .vertical_dynamic_g = Filter(input.vertical_dynamic_g,
                                     filtered.vertical_dynamic_g, alpha)};
    telemetry->filtered_g_force.push_back(
        {.timestamp = acceleration[i].timestamp, .value = filtered});
  }
  return absl::OkStatus();
}

GForceStatistics ComputeGForceStatistics(const TelemetryData& telemetry) {
  GForceStatistics statistics;
  for (const TimedSample<GForceReading>& sample :
       telemetry.filtered_g_force) {
    statistics.peak_forward_g =
        std::max(statistics.peak_forward_g, sample.value.longitudinal_g);
    statistics.peak_braking_g =
        std::max(statistics.peak_braking_g, -sample.value.longitudinal_g);
    statistics.peak_right_g =
        std::max(statistics.peak_right_g, sample.value.lateral_g);
    statistics.peak_left_g =
        std::max(statistics.peak_left_g, -sample.value.lateral_g);
    statistics.peak_vertical_dynamic_g = std::max(
        statistics.peak_vertical_dynamic_g,
        std::abs(sample.value.vertical_dynamic_g));
  }
  return statistics;
}

}  // namespace racevideo
