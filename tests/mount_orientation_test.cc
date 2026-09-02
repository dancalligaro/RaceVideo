#include "telemetry/mount_orientation.h"

#include <cmath>
#include <tuple>

#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

class MountOrientationTest
    : public testing::TestWithParam<
          std::tuple<Vector3, MountOrientation>> {};

TEST_P(MountOrientationTest, DetectsOrthogonalRollAndMakesVerticalPositive) {
  const auto& [gravity, expected] = GetParam();
  TelemetryData telemetry;
  telemetry.acceleration_metadata.values_are_camera_xyz = true;
  telemetry.angular_velocity_metadata.values_are_camera_xyz = true;
  telemetry.acceleration_meters_per_second_squared.push_back(
      {.timestamp = absl::ZeroDuration(), .value = gravity});

  const absl::Status status = DetectAndApplyMountOrientation(&telemetry);

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(telemetry.acceleration_metadata.mount_orientation, expected);
  EXPECT_TRUE(telemetry.acceleration_metadata.values_are_forward_upright);
  EXPECT_NEAR(
      telemetry.acceleration_meters_per_second_squared[0].value.z, 9.81,
      1e-9);
}

INSTANTIATE_TEST_SUITE_P(
    OrthogonalMounts, MountOrientationTest,
    testing::Values(
        std::tuple{Vector3{.x = 0, .y = 0, .z = 9.81},
                   MountOrientation::kUpright},
        std::tuple{Vector3{.x = 0, .y = 0, .z = -9.81},
                   MountOrientation::kUpsideDown},
        std::tuple{Vector3{.x = 9.81, .y = 0, .z = 0},
                   MountOrientation::kLeftSideDown},
        std::tuple{Vector3{.x = -9.81, .y = 0, .z = 0},
                   MountOrientation::kRightSideDown}));

TEST(DetectAndApplyMountOrientationTest, RejectsForwardAxisGravity) {
  TelemetryData telemetry;
  telemetry.acceleration_metadata.values_are_camera_xyz = true;
  telemetry.acceleration_meters_per_second_squared.push_back(
      {.timestamp = absl::ZeroDuration(), .value = {.x = 0, .y = 9.81, .z = 0}});

  const absl::Status status = DetectAndApplyMountOrientation(&telemetry);

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(ApplyStationaryMountCalibrationTest, UsesStationarySamplesAtEnd) {
  TelemetryData telemetry;
  telemetry.acceleration_metadata.values_are_forward_upright = true;
  telemetry.angular_velocity_metadata.values_are_forward_upright = true;
  constexpr double kTiltRadians = 0.17453292519943295;
  const Vector3 tilted_gravity = {
      .x = 0.0,
      .y = 9.81 * std::sin(kTiltRadians),
      .z = 9.81 * std::cos(kTiltRadians)};
  for (int i = 0; i <= 300; ++i) {
    const absl::Duration timestamp = absl::Milliseconds(i * 10);
    telemetry.acceleration_meters_per_second_squared.push_back(
        {.timestamp = timestamp, .value = tilted_gravity});
    telemetry.angular_velocity_radians_per_second.push_back(
        {.timestamp = timestamp, .value = {}});
  }
  telemetry.gps = {
      {.timestamp = absl::ZeroDuration(),
       .value = {.ground_speed_meters_per_second = 10.0}},
      {.timestamp = absl::Seconds(1),
       .value = {.ground_speed_meters_per_second = 10.0}},
      {.timestamp = absl::Seconds(2),
       .value = {.ground_speed_meters_per_second = 0.0}},
      {.timestamp = absl::Seconds(3),
       .value = {.ground_speed_meters_per_second = 0.0}}};

  const FineMountCalibration calibration =
      ApplyStationaryMountCalibration(&telemetry);

  ASSERT_TRUE(calibration.applied);
  EXPECT_NEAR(calibration.pitch_degrees, 10.0, 0.01);
  EXPECT_NEAR(calibration.roll_degrees, 0.0, 0.01);
  EXPECT_GT(calibration.stationary_samples, 100u);
  EXPECT_NEAR(
      telemetry.acceleration_meters_per_second_squared.back().value.y, 0.0,
      1e-9);
  EXPECT_NEAR(
      telemetry.acceleration_meters_per_second_squared.back().value.z, 9.81,
      1e-9);
}

TEST(ApplyStationaryMountCalibrationTest, FallsBackWhenGpsNeverStops) {
  TelemetryData telemetry;
  telemetry.acceleration_metadata.values_are_forward_upright = true;
  telemetry.angular_velocity_metadata.values_are_forward_upright = true;
  for (int i = 0; i <= 200; ++i) {
    const absl::Duration timestamp = absl::Milliseconds(i * 10);
    telemetry.acceleration_meters_per_second_squared.push_back(
        {.timestamp = timestamp, .value = {.z = 9.81}});
    telemetry.angular_velocity_radians_per_second.push_back(
        {.timestamp = timestamp, .value = {}});
  }
  telemetry.gps = {
      {.timestamp = absl::ZeroDuration(),
       .value = {.ground_speed_meters_per_second = 5.0}},
      {.timestamp = absl::Seconds(2),
       .value = {.ground_speed_meters_per_second = 5.0}}};

  const FineMountCalibration calibration =
      ApplyStationaryMountCalibration(&telemetry);

  EXPECT_FALSE(calibration.applied);
}

}  // namespace
}  // namespace racevideo
