#include "renderer/debug_renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(RenderDebugFramesTest, WritesRequestedPngFramesAndRefusesOverwrite) {
  TelemetryData telemetry;
  telemetry.gps = {
      {.timestamp = absl::Seconds(0),
       .value = {.latitude_degrees = 30.0,
                 .longitude_degrees = -97.0,
                 .altitude_meters = 0,
                 .ground_speed_meters_per_second = 10,
                 .speed_3d_meters_per_second = 10}},
      {.timestamp = absl::Seconds(1),
       .value = {.latitude_degrees = 30.001,
                 .longitude_degrees = -97.0,
                 .altitude_meters = 0,
                 .ground_speed_meters_per_second = 11,
                 .speed_3d_meters_per_second = 11}}};
  telemetry.filtered_g_force = {
      {.timestamp = absl::Seconds(0),
       .value = {.lateral_g = 0,
                 .longitudinal_g = 0,
                 .vertical_dynamic_g = 0}},
      {.timestamp = absl::Seconds(1),
       .value = {.lateral_g = 0.2,
                 .longitudinal_g = 0.1,
                 .vertical_dynamic_g = 0}}};
  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
  ASSERT_TRUE(overlay.ok()) << overlay.status();
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      "racevideo_debug_renderer_test";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  ASSERT_FALSE(error);
  const DebugRenderOptions options = {
      .output_directory = directory,
      .start_seconds = 0,
      .duration_seconds = 0.2,
      .frames_per_second = 10,
      .width = 320,
      .height = 180};

  ASSERT_TRUE(RenderDebugFrames(telemetry, *overlay, options).ok());
  EXPECT_TRUE(std::filesystem::exists(directory / "frame_000000.png"));
  EXPECT_TRUE(std::filesystem::exists(directory / "frame_000001.png"));
  EXPECT_FALSE(std::filesystem::exists(directory / "frame_000002.png"));
  std::ifstream input(directory / "frame_000000.png", std::ios::binary);
  std::array<unsigned char, 8> signature{};
  input.read(reinterpret_cast<char*>(signature.data()), signature.size());
  input.close();
  const std::array<unsigned char, 8> expected =
      {137, 80, 78, 71, 13, 10, 26, 10};
  EXPECT_EQ(signature, expected);
  EXPECT_EQ(RenderDebugFrames(telemetry, *overlay, options).code(),
            absl::StatusCode::kAlreadyExists);
  std::filesystem::remove_all(directory, error);
  EXPECT_FALSE(error);
}

TEST(RenderOverlayFrameRgbaTest, ReturnsOneTransparentRgbaImage) {
  TelemetryData telemetry;
  telemetry.gps = {
      {.timestamp = absl::Seconds(0),
       .value = {.latitude_degrees = 30.0,
                 .longitude_degrees = -97.0,
                 .ground_speed_meters_per_second = 10}},
      {.timestamp = absl::Seconds(1),
       .value = {.latitude_degrees = 30.001,
                 .longitude_degrees = -97.0,
                 .ground_speed_meters_per_second = 11}}};
  telemetry.filtered_g_force = {
      {.timestamp = absl::Seconds(0), .value = {}},
      {.timestamp = absl::Seconds(1), .value = {}}};
  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
  ASSERT_TRUE(overlay.ok()) << overlay.status();

  const absl::StatusOr<std::vector<std::uint8_t>> pixels =
      RenderOverlayFrameRgba(telemetry, *overlay, 0.5, 320, 180,
                             SpeedUnit::kKilometersPerHour);

  ASSERT_TRUE(pixels.ok()) << pixels.status();
  ASSERT_EQ(pixels->size(), 320u * 180u * 4u);
  bool has_transparent_pixel = false;
  bool has_visible_pixel = false;
  for (std::size_t index = 3; index < pixels->size(); index += 4) {
    has_transparent_pixel = has_transparent_pixel || (*pixels)[index] == 0;
    has_visible_pixel = has_visible_pixel || (*pixels)[index] != 0;
  }
  EXPECT_TRUE(has_transparent_pixel);
  EXPECT_TRUE(has_visible_pixel);
}

TEST(RenderOverlayFrameRgbaTest, Fits999MphWithRightPadding) {
  TelemetryData telemetry;
  telemetry.gps = {
      {.timestamp = absl::Seconds(0),
       .value = {.latitude_degrees = 30.0,
                 .longitude_degrees = -97.0,
                 .ground_speed_meters_per_second = 1000}},
      {.timestamp = absl::Seconds(1),
       .value = {.latitude_degrees = 30.001,
                 .longitude_degrees = -97.0,
                 .ground_speed_meters_per_second = 1000}}};
  telemetry.filtered_g_force = {
      {.timestamp = absl::Seconds(0), .value = {}},
      {.timestamp = absl::Seconds(1), .value = {}}};
  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
  ASSERT_TRUE(overlay.ok()) << overlay.status();
  constexpr int kWidth = 320;
  constexpr int kHeight = 180;

  const absl::StatusOr<std::vector<std::uint8_t>> pixels =
      RenderOverlayFrameRgba(telemetry, *overlay, 0.5, kWidth, kHeight,
                             SpeedUnit::kMilesPerHour);

  ASSERT_TRUE(pixels.ok()) << pixels.status();
  // At 180p, the renderer uses a 3-pixel digit stroke and a 16-pixel margin.
  // "999 MPH" produces an 86-pixel-wide panel, whose right border is x=101.
  constexpr int kPanelLeft = 16;
  constexpr int kPanelTop = 16;
  constexpr int kPanelHeight = 27;
  constexpr int kRightBorderX = 101;
  int rightmost_text_x = -1;
  for (int y = kPanelTop + 1; y < kPanelTop + kPanelHeight - 1; ++y) {
    for (int x = kPanelLeft + 1; x < kRightBorderX; ++x) {
      const std::size_t alpha_index =
          (static_cast<std::size_t>(y) * kWidth + x) * 4 + 3;
      const std::uint8_t red = (*pixels)[alpha_index - 3];
      // The muted MPH glyph is more opaque and brighter than the panel, but
      // darker than the white number and border.
      if ((*pixels)[alpha_index] > 240 && red > 100 && red < 200) {
        rightmost_text_x = std::max(rightmost_text_x, x);
      }
    }
  }
  ASSERT_GE(rightmost_text_x, 0);
  constexpr int kRequiredRightPaddingPixels = 4;
  EXPECT_GE(kRightBorderX - rightmost_text_x - 1,
            kRequiredRightPaddingPixels);
}

}  // namespace
}  // namespace racevideo
