#include "telemetry/mount_orientation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/status/status.h"

namespace racevideo {
namespace {

constexpr double kMinimumMeanAcceleration = 1.0;
constexpr double kMinimumOrientationConfidence = 0.7;

Vector3 RotateToUpright(const Vector3& input,
                        MountOrientation orientation) {
  switch (orientation) {
    case MountOrientation::kUpright:
      return input;
    case MountOrientation::kUpsideDown:
      return {.x = -input.x, .y = input.y, .z = -input.z};
    case MountOrientation::kLeftSideDown:
      return {.x = -input.z, .y = input.y, .z = input.x};
    case MountOrientation::kRightSideDown:
      return {.x = input.z, .y = input.y, .z = -input.x};
    case MountOrientation::kUnknown:
      return input;
  }
  return input;
}

void RotateSamples(MountOrientation orientation,
                   std::vector<TimedSample<Vector3>>* samples) {
  for (TimedSample<Vector3>& sample : *samples) {
    sample.value = RotateToUpright(sample.value, orientation);
  }
}

void SetMetadata(MountOrientation orientation, double confidence,
                 InertialStreamMetadata* metadata) {
  metadata->mount_orientation = orientation;
  metadata->mount_orientation_confidence = confidence;
  metadata->values_are_forward_upright = true;
}

}  // namespace

absl::Status DetectAndApplyMountOrientation(TelemetryData* telemetry) {
  if (!telemetry->acceleration_metadata.values_are_camera_xyz) {
    return absl::FailedPreconditionError(
        "camera XYZ normalization is required before mount detection");
  }
  if (telemetry->acceleration_meters_per_second_squared.empty()) {
    return absl::FailedPreconditionError(
        "accelerometer samples are required for mount detection");
  }

  long double sum_x = 0;
  long double sum_y = 0;
  long double sum_z = 0;
  for (const TimedSample<Vector3>& sample :
       telemetry->acceleration_meters_per_second_squared) {
    sum_x += sample.value.x;
    sum_y += sample.value.y;
    sum_z += sample.value.z;
  }
  const long double count = static_cast<long double>(
      telemetry->acceleration_meters_per_second_squared.size());
  const double mean_x = static_cast<double>(sum_x / count);
  const double mean_y = static_cast<double>(sum_y / count);
  const double mean_z = static_cast<double>(sum_z / count);
  const double magnitude =
      std::sqrt(mean_x * mean_x + mean_y * mean_y + mean_z * mean_z);
  if (!std::isfinite(magnitude) || magnitude < kMinimumMeanAcceleration) {
    return absl::FailedPreconditionError(
        "accelerometer gravity direction is too weak for mount detection");
  }
  const double confidence = std::max(std::abs(mean_x), std::abs(mean_z)) /
                            magnitude;
  if (confidence < kMinimumOrientationConfidence) {
    return absl::FailedPreconditionError(
        "camera mount is not close to a supported orthogonal roll");
  }

  MountOrientation orientation;
  if (std::abs(mean_z) >= std::abs(mean_x)) {
    orientation = mean_z >= 0 ? MountOrientation::kUpright
                              : MountOrientation::kUpsideDown;
  } else {
    orientation = mean_x >= 0 ? MountOrientation::kLeftSideDown
                              : MountOrientation::kRightSideDown;
  }
  RotateSamples(orientation,
                &telemetry->acceleration_meters_per_second_squared);
  RotateSamples(orientation,
                &telemetry->angular_velocity_radians_per_second);
  SetMetadata(orientation, confidence, &telemetry->acceleration_metadata);
  SetMetadata(orientation, confidence, &telemetry->angular_velocity_metadata);
  return absl::OkStatus();
}

std::string_view MountOrientationName(MountOrientation orientation) {
  switch (orientation) {
    case MountOrientation::kUpright:
      return "upright";
    case MountOrientation::kUpsideDown:
      return "upside_down";
    case MountOrientation::kLeftSideDown:
      return "left_side_down";
    case MountOrientation::kRightSideDown:
      return "right_side_down";
    case MountOrientation::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace racevideo
