#include "telemetry/telemetry.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(TelemetryDataTest, KeepsSensorsOnIndependentTimelines) {
  TelemetryData telemetry;
  telemetry.gps.push_back({
      .timestamp = absl::Milliseconds(100),
      .value = {.latitude_degrees = 30.0,
                .longitude_degrees = -97.0,
                .altitude_meters = 200.0,
                .ground_speed_meters_per_second = 20.0,
                .speed_3d_meters_per_second = 20.5},
  });
  telemetry.acceleration_meters_per_second_squared.push_back({
      .timestamp = absl::Milliseconds(5),
      .value = {.x = 1.0, .y = 2.0, .z = 3.0},
  });

  EXPECT_EQ(telemetry.gps.size(), 1);
  EXPECT_EQ(telemetry.acceleration_meters_per_second_squared.size(), 1);
  EXPECT_EQ(telemetry.gps.front().timestamp, absl::Milliseconds(100));
}

}  // namespace
}  // namespace racevideo

