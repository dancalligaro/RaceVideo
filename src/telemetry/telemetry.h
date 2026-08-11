#ifndef RACEVIDEO_TELEMETRY_TELEMETRY_H_
#define RACEVIDEO_TELEMETRY_TELEMETRY_H_

#include <vector>

#include "absl/time/time.h"

namespace racevideo {

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

struct TelemetryData {
  std::vector<TimedSample<GpsReading>> gps;
  std::vector<TimedSample<Vector3>> acceleration_meters_per_second_squared;
  std::vector<TimedSample<Vector3>> angular_velocity_radians_per_second;
};

}  // namespace racevideo

#endif  // RACEVIDEO_TELEMETRY_TELEMETRY_H_

