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
constexpr double kStationarySpeedMetersPerSecond = 0.5;
constexpr double kMaximumGyroRadiansPerSecond = 0.15;
constexpr double kMinimumGravity = 8.0;
constexpr double kMaximumGravity = 11.5;
constexpr double kMaximumGpsDistanceSeconds = 1.0;
constexpr double kMinimumStationaryCoverageSeconds = 1.0;
constexpr std::size_t kMinimumStationarySamples = 100;
constexpr double kMinimumGravityDirectionAgreement = 0.995;
constexpr double kMaximumFineCorrectionDegrees = 20.0;
constexpr double kRadiansToDegrees = 57.2957795130823208768;

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

double Magnitude(const Vector3& value) {
  return std::sqrt(value.x * value.x + value.y * value.y +
                   value.z * value.z);
}

template <typename Value>
const TimedSample<Value>* NearestSample(
    const std::vector<TimedSample<Value>>& samples, absl::Duration timestamp,
    std::size_t* index) {
  if (samples.empty()) return nullptr;
  while (*index + 1 < samples.size() &&
         samples[*index + 1].timestamp <= timestamp) {
    ++*index;
  }
  if (*index + 1 < samples.size() &&
      timestamp - samples[*index].timestamp >
          samples[*index + 1].timestamp - timestamp) {
    return &samples[*index + 1];
  }
  return &samples[*index];
}

Vector3 RotateGravityToVertical(const Vector3& value,
                                const Vector3& gravity_unit) {
  const Vector3 cross = {
      .x = gravity_unit.y, .y = -gravity_unit.x, .z = 0.0};
  const double cosine = gravity_unit.z;
  const Vector3 cross_value = {
      .x = cross.y * value.z - cross.z * value.y,
      .y = cross.z * value.x - cross.x * value.z,
      .z = cross.x * value.y - cross.y * value.x};
  const Vector3 cross_twice = {
      .x = cross.y * cross_value.z - cross.z * cross_value.y,
      .y = cross.z * cross_value.x - cross.x * cross_value.z,
      .z = cross.x * cross_value.y - cross.y * cross_value.x};
  const double scale = 1.0 / (1.0 + cosine);
  return {.x = value.x + cross_value.x + cross_twice.x * scale,
          .y = value.y + cross_value.y + cross_twice.y * scale,
          .z = value.z + cross_value.z + cross_twice.z * scale};
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

FineMountCalibration ApplyStationaryMountCalibration(
    TelemetryData* telemetry) {
  FineMountCalibration result;
  if (!telemetry->acceleration_metadata.values_are_forward_upright ||
      telemetry->gps.empty() ||
      telemetry->acceleration_meters_per_second_squared.empty() ||
      telemetry->angular_velocity_radians_per_second.empty()) {
    return result;
  }

  std::vector<const TimedSample<Vector3>*> candidates;
  std::size_t gps_index = 0;
  std::size_t gyro_index = 0;
  double coverage_seconds = 0.0;
  absl::Duration previous_candidate = absl::ZeroDuration();
  bool has_previous_candidate = false;
  for (const TimedSample<Vector3>& acceleration :
       telemetry->acceleration_meters_per_second_squared) {
    const TimedSample<GpsReading>* gps =
        NearestSample(telemetry->gps, acceleration.timestamp, &gps_index);
    const TimedSample<Vector3>* gyro = NearestSample(
        telemetry->angular_velocity_radians_per_second,
        acceleration.timestamp, &gyro_index);
    if (gps == nullptr || gyro == nullptr) continue;
    const double gps_distance = std::abs(absl::ToDoubleSeconds(
        acceleration.timestamp - gps->timestamp));
    const double acceleration_magnitude = Magnitude(acceleration.value);
    if (gps_distance > kMaximumGpsDistanceSeconds ||
        gps->value.ground_speed_meters_per_second >
            kStationarySpeedMetersPerSecond ||
        Magnitude(gyro->value) > kMaximumGyroRadiansPerSecond ||
        acceleration_magnitude < kMinimumGravity ||
        acceleration_magnitude > kMaximumGravity) {
      continue;
    }
    if (has_previous_candidate) {
      const double gap = absl::ToDoubleSeconds(acceleration.timestamp -
                                                previous_candidate);
      if (gap > 0.0 && gap <= 0.05) coverage_seconds += gap;
    }
    previous_candidate = acceleration.timestamp;
    has_previous_candidate = true;
    candidates.push_back(&acceleration);
  }
  result.stationary_samples = candidates.size();
  if (candidates.size() < kMinimumStationarySamples ||
      coverage_seconds < kMinimumStationaryCoverageSeconds) {
    return result;
  }

  Vector3 mean{};
  for (const TimedSample<Vector3>* sample : candidates) {
    const double magnitude = Magnitude(sample->value);
    mean.x += sample->value.x / magnitude;
    mean.y += sample->value.y / magnitude;
    mean.z += sample->value.z / magnitude;
  }
  const double mean_magnitude = Magnitude(mean);
  const double direction_agreement =
      mean_magnitude / static_cast<double>(candidates.size());
  if (!std::isfinite(mean_magnitude) || mean_magnitude == 0.0 ||
      direction_agreement < kMinimumGravityDirectionAgreement) {
    return result;
  }
  const Vector3 gravity = {.x = mean.x / mean_magnitude,
                           .y = mean.y / mean_magnitude,
                           .z = mean.z / mean_magnitude};
  if (gravity.z <= 0.0) return result;

  result.pitch_degrees =
      std::atan2(gravity.y, gravity.z) * kRadiansToDegrees;
  result.roll_degrees =
      -std::atan2(gravity.x, gravity.z) * kRadiansToDegrees;
  if (std::abs(result.pitch_degrees) > kMaximumFineCorrectionDegrees ||
      std::abs(result.roll_degrees) > kMaximumFineCorrectionDegrees) {
    result.pitch_degrees = 0.0;
    result.roll_degrees = 0.0;
    return result;
  }
  for (TimedSample<Vector3>& sample :
       telemetry->acceleration_meters_per_second_squared) {
    sample.value = RotateGravityToVertical(sample.value, gravity);
  }
  for (TimedSample<Vector3>& sample :
       telemetry->angular_velocity_radians_per_second) {
    sample.value = RotateGravityToVertical(sample.value, gravity);
  }
  result.applied = true;
  return result;
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
