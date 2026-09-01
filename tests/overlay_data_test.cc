#include "overlay/overlay_data.h"

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TimedSample<GpsReading> Gps(double seconds, double latitude,
                            double longitude, double speed) {
  return {.timestamp = absl::Seconds(seconds),
          .value = {.latitude_degrees = latitude,
                    .longitude_degrees = longitude,
                    .altitude_meters = 0,
                    .ground_speed_meters_per_second = speed,
                    .speed_3d_meters_per_second = speed}};
}

TEST(BuildOverlayDataTest, CreatesNorthUpAspectPreservingTrack) {
  TelemetryData telemetry;
  telemetry.gps = {Gps(0, 30.0, -97.0, 10),
                   Gps(1, 30.001, -97.0, 11),
                   Gps(2, 30.002, -97.0, 12)};

  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);

  ASSERT_TRUE(overlay.ok()) << overlay.status();
  ASSERT_EQ(overlay->track.size(), 3);
  EXPECT_NEAR(overlay->track.front().x, 0.5, 1e-9);
  EXPECT_GT(overlay->track.front().y, overlay->track.back().y);
  EXPECT_NEAR(overlay->track.front().y, 0.95, 1e-9);
  EXPECT_NEAR(overlay->track.back().y, 0.05, 1e-9);
  EXPECT_NEAR(overlay->navigation.front().heading_degrees, 0.0, 1e-9);
}

TEST(BuildOverlayDataTest, ComputesEastboundCompassHeading) {
  TelemetryData telemetry;
  telemetry.gps = {Gps(0, 30.0, -97.0, 10),
                   Gps(1, 30.0, -96.999, 10)};

  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);

  ASSERT_TRUE(overlay.ok()) << overlay.status();
  EXPECT_NEAR(overlay->navigation.front().heading_degrees, 90.0, 1e-9);
}

TEST(BuildOverlayDataTest, IgnoresImplausibleGpsJump) {
  TelemetryData telemetry;
  telemetry.gps = {Gps(0, 30.0, -97.0, 10),
                   Gps(1, 40.0, -80.0, 10),
                   Gps(2, 30.001, -97.0, 10)};

  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);

  ASSERT_TRUE(overlay.ok()) << overlay.status();
  EXPECT_EQ(overlay->track.size(), 2);
  EXPECT_NEAR(overlay->track.front().x, 0.5, 1e-9);
}

TEST(BuildOverlayDataTest, ClipsTrackToRequestedRenderRange) {
  TelemetryData telemetry;
  telemetry.gps = {Gps(0, 30.000, -97.0, 10),
                   Gps(10, 30.001, -97.0, 11),
                   Gps(20, 30.002, -97.0, 12),
                   Gps(30, 30.003, -97.0, 13)};

  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(
      telemetry, absl::Seconds(5), absl::Seconds(25));

  ASSERT_TRUE(overlay.ok()) << overlay.status();
  ASSERT_EQ(overlay->track.size(), 4);
  EXPECT_EQ(overlay->track.front().timestamp, absl::Seconds(5));
  EXPECT_EQ(overlay->track.back().timestamp, absl::Seconds(25));
  EXPECT_EQ(overlay->navigation.front().timestamp, absl::Seconds(5));
  EXPECT_EQ(overlay->navigation.back().timestamp, absl::Seconds(25));
}

TEST(SampleOverlayFrameTest, InterpolatesValuesAndSplitsTrackProgress) {
  TelemetryData telemetry;
  telemetry.gps = {Gps(0, 30.0, -97.0, 10),
                   Gps(10, 30.001, -97.0, 20)};
  telemetry.filtered_g_force = {
      {.timestamp = absl::Seconds(0),
       .value = {.lateral_g = 0,
                 .longitudinal_g = 0,
                 .vertical_dynamic_g = 0}},
      {.timestamp = absl::Seconds(10),
       .value = {.lateral_g = 1,
                 .longitudinal_g = -1,
                 .vertical_dynamic_g = 0.5}}};
  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
  ASSERT_TRUE(overlay.ok()) << overlay.status();

  const absl::StatusOr<OverlayFrameData> frame =
      SampleOverlayFrame(telemetry, *overlay, absl::Seconds(5));

  ASSERT_TRUE(frame.ok()) << frame.status();
  EXPECT_DOUBLE_EQ(frame->speed_meters_per_second, 15.0);
  EXPECT_DOUBLE_EQ(frame->g_force.lateral_g, 0.5);
  EXPECT_DOUBLE_EQ(frame->g_force.longitudinal_g, -0.5);
  EXPECT_DOUBLE_EQ(frame->g_force.vertical_dynamic_g, 0.25);
  EXPECT_EQ(frame->explored_track_point_count, 1);
}

}  // namespace
}  // namespace racevideo
