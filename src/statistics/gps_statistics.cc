#include "statistics/gps_statistics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace racevideo {
namespace {

constexpr double kEarthRadiusMeters = 6'371'008.8;
constexpr double kDegreesToRadians = 0.01745329251994329577;
constexpr double kMovingSpeedThresholdMetersPerSecond = 0.5;
constexpr double kMaximumPlausibleSegmentSpeedMetersPerSecond = 150.0;
constexpr double kAltitudeFilterTimeConstantSeconds = 3.0;
constexpr double kElevationDeadbandMeters = 3.0;

bool IsValid(const GpsReading& reading) {
  return std::isfinite(reading.latitude_degrees) &&
         std::isfinite(reading.longitude_degrees) &&
         std::isfinite(reading.altitude_meters) &&
         std::isfinite(reading.ground_speed_meters_per_second) &&
         reading.latitude_degrees >= -90.0 &&
         reading.latitude_degrees <= 90.0 &&
         reading.longitude_degrees >= -180.0 &&
         reading.longitude_degrees <= 180.0 &&
         reading.ground_speed_meters_per_second >= 0.0;
}

double DistanceMeters(const GpsReading& first, const GpsReading& second) {
  const double latitude_1 = first.latitude_degrees * kDegreesToRadians;
  const double latitude_2 = second.latitude_degrees * kDegreesToRadians;
  const double delta_latitude = latitude_2 - latitude_1;
  const double delta_longitude =
      (second.longitude_degrees - first.longitude_degrees) *
      kDegreesToRadians;
  const double sin_latitude = std::sin(delta_latitude / 2.0);
  const double sin_longitude = std::sin(delta_longitude / 2.0);
  const double haversine =
      sin_latitude * sin_latitude + std::cos(latitude_1) *
                                            std::cos(latitude_2) *
                                            sin_longitude * sin_longitude;
  return 2.0 * kEarthRadiusMeters *
         std::asin(std::sqrt(std::clamp(haversine, 0.0, 1.0)));
}

}  // namespace

absl::StatusOr<GpsStatistics> ComputeGpsStatistics(
    const TelemetryData& telemetry) {
  GpsStatistics statistics;
  std::optional<TimedSample<GpsReading>> previous;
  std::optional<double> filtered_altitude;
  std::optional<double> elevation_reference;
  double moving_distance_meters = 0.0;

  for (const TimedSample<GpsReading>& sample : telemetry.gps) {
    if (!IsValid(sample.value)) continue;
    statistics.maximum_speed_meters_per_second =
        std::max(statistics.maximum_speed_meters_per_second,
                 sample.value.ground_speed_meters_per_second);
    if (!previous.has_value()) {
      previous = sample;
      filtered_altitude = sample.value.altitude_meters;
      elevation_reference = *filtered_altitude;
      continue;
    }

    const double delta_seconds =
        absl::ToDoubleSeconds(sample.timestamp - previous->timestamp);
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) {
      return absl::DataLossError(
          "GPS timestamps must be strictly increasing");
    }
    const double distance_meters =
        DistanceMeters(previous->value, sample.value);
    const double segment_speed = distance_meters / delta_seconds;
    if (std::isfinite(distance_meters) &&
        segment_speed <= kMaximumPlausibleSegmentSpeedMetersPerSecond) {
      statistics.distance_meters += distance_meters;
      const double recorded_speed =
          (previous->value.ground_speed_meters_per_second +
           sample.value.ground_speed_meters_per_second) /
          2.0;
      if (recorded_speed >= kMovingSpeedThresholdMetersPerSecond) {
        statistics.moving_time_seconds += delta_seconds;
        moving_distance_meters += distance_meters;
      }
    }

    const double alpha = delta_seconds /
                         (kAltitudeFilterTimeConstantSeconds + delta_seconds);
    *filtered_altitude +=
        alpha * (sample.value.altitude_meters - *filtered_altitude);
    const double elevation_change = *filtered_altitude - *elevation_reference;
    if (elevation_change >= kElevationDeadbandMeters) {
      statistics.elevation_gain_meters += elevation_change;
      *elevation_reference = *filtered_altitude;
    } else if (elevation_change <= -kElevationDeadbandMeters) {
      *elevation_reference = *filtered_altitude;
    }
    previous = sample;
  }

  if (!previous.has_value()) {
    return absl::FailedPreconditionError("no valid GPS samples");
  }
  if (statistics.moving_time_seconds > 0.0) {
    statistics.average_moving_speed_meters_per_second =
        moving_distance_meters / statistics.moving_time_seconds;
  }
  return statistics;
}

}  // namespace racevideo
