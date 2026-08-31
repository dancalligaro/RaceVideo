#ifndef RACEVIDEO_STATISTICS_GPS_STATISTICS_H_
#define RACEVIDEO_STATISTICS_GPS_STATISTICS_H_

#include "absl/status/statusor.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct GpsStatistics {
  double maximum_speed_meters_per_second = 0.0;
  double average_moving_speed_meters_per_second = 0.0;
  double distance_meters = 0.0;
  double moving_time_seconds = 0.0;
  double elevation_gain_meters = 0.0;
};

absl::StatusOr<GpsStatistics> ComputeGpsStatistics(
    const TelemetryData& telemetry);

}  // namespace racevideo

#endif  // RACEVIDEO_STATISTICS_GPS_STATISTICS_H_
