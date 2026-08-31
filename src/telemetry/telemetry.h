#ifndef RACEVIDEO_TELEMETRY_TELEMETRY_H_
#define RACEVIDEO_TELEMETRY_TELEMETRY_H_

#include <string>
#include <vector>

#include "absl/time/time.h"

namespace racevideo {

enum class MountOrientation {
  kUnknown,
  kUpright,
  kUpsideDown,
  kLeftSideDown,
  kRightSideDown,
};

template <typename T>
struct TimedSample {
  absl::Duration timestamp;
  T value;

  bool operator==(const TimedSample&) const = default;
};

struct GpsReading {
  double latitude_degrees;
  double longitude_degrees;
  double altitude_meters;
  double ground_speed_meters_per_second;
  double speed_3d_meters_per_second;

  bool operator==(const GpsReading&) const = default;
};

struct Vector3 {
  double x;
  double y;
  double z;

  bool operator==(const Vector3&) const = default;
};

struct GForceReading {
  double lateral_g;
  double longitudinal_g;
  double vertical_dynamic_g;

  bool operator==(const GForceReading&) const = default;
};

struct InertialStreamMetadata {
  // Empty when the camera did not declare an order. Uppercase means positive
  // and lowercase means negative; for example, "YxZ" means Y, -X, Z.
  std::string source_axis_order;
  bool values_are_camera_xyz = false;
  MountOrientation mount_orientation = MountOrientation::kUnknown;
  double mount_orientation_confidence = 0.0;
  bool values_are_forward_upright = false;

  bool operator==(const InertialStreamMetadata&) const = default;
};

struct TelemetryData {
  std::vector<TimedSample<GpsReading>> gps;
  std::vector<TimedSample<Vector3>> acceleration_meters_per_second_squared;
  std::vector<TimedSample<Vector3>> angular_velocity_radians_per_second;
  InertialStreamMetadata acceleration_metadata;
  InertialStreamMetadata angular_velocity_metadata;
  std::vector<TimedSample<GForceReading>> filtered_g_force;
  double g_force_filter_cutoff_hz = 0.0;
};

}  // namespace racevideo

#endif  // RACEVIDEO_TELEMETRY_TELEMETRY_H_
