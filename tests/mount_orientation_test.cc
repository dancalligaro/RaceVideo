#include "telemetry/mount_orientation.h"

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

}  // namespace
}  // namespace racevideo
