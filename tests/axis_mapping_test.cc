#include "telemetry/axis_mapping.h"

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(NormalizeInertialAxesTest, MapsSignedComponentsToCameraXyz) {
  TelemetryData telemetry;
  telemetry.acceleration_meters_per_second_squared.push_back(
      {.timestamp = absl::ZeroDuration(),
       .value = {.x = 10, .y = 20, .z = 30}});
  telemetry.angular_velocity_radians_per_second.push_back(
      {.timestamp = absl::ZeroDuration(),
       .value = {.x = 1, .y = 2, .z = 3}});

  const absl::Status status = NormalizeInertialAxes("YxZ", &telemetry);

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(telemetry.acceleration_meters_per_second_squared[0].value,
            (Vector3{.x = -20, .y = 10, .z = 30}));
  EXPECT_EQ(telemetry.angular_velocity_radians_per_second[0].value,
            (Vector3{.x = -2, .y = 1, .z = 3}));
  EXPECT_EQ(telemetry.acceleration_metadata.source_axis_order, "YxZ");
  EXPECT_TRUE(telemetry.acceleration_metadata.values_are_camera_xyz);
}

TEST(NormalizeInertialAxesTest, RejectsDuplicateAxes) {
  TelemetryData telemetry;

  const absl::Status status = NormalizeInertialAxes("XXZ", &telemetry);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace racevideo
