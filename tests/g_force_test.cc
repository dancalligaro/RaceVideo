#include "telemetry/g_force.h"

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(GenerateFilteredGForceTest, RemovesRestingVerticalGravity) {
  TelemetryData telemetry;
  telemetry.acceleration_metadata.values_are_forward_upright = true;
  telemetry.acceleration_meters_per_second_squared = {
      {.timestamp = absl::ZeroDuration(),
       .value = {.x = 0,
                 .y = 0,
                 .z = kStandardGravityMetersPerSecondSquared}},
      {.timestamp = absl::Milliseconds(10),
       .value = {.x = 0,
                 .y = 0,
                 .z = kStandardGravityMetersPerSecondSquared}}};

  const absl::Status status =
      GenerateFilteredGForce(kDefaultGForceFilterCutoffHz, &telemetry);

  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(telemetry.filtered_g_force.size(), 2);
  EXPECT_DOUBLE_EQ(telemetry.filtered_g_force.back().value.lateral_g, 0.0);
  EXPECT_DOUBLE_EQ(telemetry.filtered_g_force.back().value.longitudinal_g,
                   0.0);
  EXPECT_DOUBLE_EQ(telemetry.filtered_g_force.back().value.vertical_dynamic_g,
                   0.0);
}

TEST(GenerateFilteredGForceTest, SmoothsAOneGForwardStep) {
  TelemetryData telemetry;
  telemetry.acceleration_metadata.values_are_forward_upright = true;
  telemetry.acceleration_meters_per_second_squared = {
      {.timestamp = absl::ZeroDuration(),
       .value = {.x = 0,
                 .y = 0,
                 .z = kStandardGravityMetersPerSecondSquared}},
      {.timestamp = absl::Milliseconds(10),
       .value = {.x = 0,
                 .y = kStandardGravityMetersPerSecondSquared,
                 .z = kStandardGravityMetersPerSecondSquared}}};

  const absl::Status status = GenerateFilteredGForce(5.0, &telemetry);

  ASSERT_TRUE(status.ok()) << status;
  const double filtered =
      telemetry.filtered_g_force.back().value.longitudinal_g;
  EXPECT_GT(filtered, 0.0);
  EXPECT_LT(filtered, 1.0);
}

TEST(GenerateFilteredGForceTest, RequiresNormalizedMount) {
  TelemetryData telemetry;

  const absl::Status status = GenerateFilteredGForce(5.0, &telemetry);

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(ComputeGForceStatisticsTest, ReportsDirectionalPeaks) {
  TelemetryData telemetry;
  telemetry.filtered_g_force = {
      {.value = {.lateral_g = -0.4,
                 .longitudinal_g = 0.7,
                 .vertical_dynamic_g = 0.2}},
      {.value = {.lateral_g = 0.6,
                 .longitudinal_g = -0.8,
                 .vertical_dynamic_g = -0.9}}};

  const GForceStatistics statistics = ComputeGForceStatistics(telemetry);

  EXPECT_DOUBLE_EQ(statistics.peak_forward_g, 0.7);
  EXPECT_DOUBLE_EQ(statistics.peak_braking_g, 0.8);
  EXPECT_DOUBLE_EQ(statistics.peak_left_g, 0.4);
  EXPECT_DOUBLE_EQ(statistics.peak_right_g, 0.6);
  EXPECT_DOUBLE_EQ(statistics.peak_vertical_dynamic_g, 0.9);
}

}  // namespace
}  // namespace racevideo
