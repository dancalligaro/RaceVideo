#include "statistics/gps_statistics.h"

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(ComputeGpsStatisticsTest, ComputesDistanceSpeedTimeAndElevation) {
  TelemetryData telemetry;
  telemetry.gps = {
      {.timestamp = absl::Seconds(0),
       .value = {.latitude_degrees = 0,
                 .longitude_degrees = 0,
                 .altitude_meters = 100,
                 .ground_speed_meters_per_second = 10,
                 .speed_3d_meters_per_second = 10}},
      {.timestamp = absl::Seconds(10),
       .value = {.latitude_degrees = 0,
                 .longitude_degrees = 0.00089932,
                 .altitude_meters = 110,
                 .ground_speed_meters_per_second = 12,
                 .speed_3d_meters_per_second = 12}},
      {.timestamp = absl::Seconds(20),
       .value = {.latitude_degrees = 0,
                 .longitude_degrees = 0.00179864,
                 .altitude_meters = 120,
                 .ground_speed_meters_per_second = 15,
                 .speed_3d_meters_per_second = 15}}};

  const absl::StatusOr<GpsStatistics> statistics =
      ComputeGpsStatistics(telemetry);

  ASSERT_TRUE(statistics.ok()) << statistics.status();
  EXPECT_NEAR(statistics->distance_meters, 200.0, 0.2);
  EXPECT_NEAR(statistics->moving_time_seconds, 20.0, 1e-9);
  EXPECT_NEAR(statistics->average_moving_speed_meters_per_second, 10.0, 0.02);
  EXPECT_DOUBLE_EQ(statistics->maximum_speed_meters_per_second, 15.0);
  EXPECT_GT(statistics->elevation_gain_meters, 0.0);
}

TEST(ComputeGpsStatisticsTest, ExcludesStoppedTimeFromMovingAverage) {
  TelemetryData telemetry;
  telemetry.gps = {
      {.timestamp = absl::Seconds(0),
       .value = {.latitude_degrees = 0,
                 .longitude_degrees = 0,
                 .altitude_meters = 0,
                 .ground_speed_meters_per_second = 0}},
      {.timestamp = absl::Seconds(10),
       .value = {.latitude_degrees = 0,
                 .longitude_degrees = 0,
                 .altitude_meters = 0,
                 .ground_speed_meters_per_second = 0}},
      {.timestamp = absl::Seconds(20),
       .value = {.latitude_degrees = 0,
                 .longitude_degrees = 0.00089932,
                 .altitude_meters = 0,
                 .ground_speed_meters_per_second = 10}}};

  const absl::StatusOr<GpsStatistics> statistics =
      ComputeGpsStatistics(telemetry);

  ASSERT_TRUE(statistics.ok()) << statistics.status();
  EXPECT_NEAR(statistics->moving_time_seconds, 10.0, 1e-9);
  EXPECT_NEAR(statistics->average_moving_speed_meters_per_second, 10.0, 0.02);
}

TEST(ComputeGpsStatisticsTest, RejectsNonIncreasingTimestamps) {
  TelemetryData telemetry;
  const GpsReading reading = {.latitude_degrees = 0,
                              .longitude_degrees = 0,
                              .altitude_meters = 0,
                              .ground_speed_meters_per_second = 1};
  telemetry.gps = {{.timestamp = absl::Seconds(1), .value = reading},
                   {.timestamp = absl::Seconds(1), .value = reading}};

  const absl::StatusOr<GpsStatistics> statistics =
      ComputeGpsStatistics(telemetry);

  EXPECT_EQ(statistics.status().code(), absl::StatusCode::kDataLoss);
}

}  // namespace
}  // namespace racevideo
