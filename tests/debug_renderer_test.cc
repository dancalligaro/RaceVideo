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
      RenderOverlayFrameRgba(telemetry, *overlay, 0.5, 320, 180, {});

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
  for (int y = 0; y < 70; ++y) {
    for (int x = 0; x < 100; ++x) {
      const std::size_t alpha_index =
          (static_cast<std::size_t>(y) * 320 + x) * 4 + 3;
      EXPECT_EQ((*pixels)[alpha_index], 0);
    }
  }
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
                             {SpeedUnit::kMilesPerHour,
                              SpeedUnit::kKilometersPerHour});

  ASSERT_TRUE(pixels.ok()) << pixels.status();
  // At 180p, the compact renderer uses a 2-pixel digit stroke and a 16-pixel
  // margin. The selected MPH row appears first and leaves at least four pixels
  // after its unit label.
  constexpr int kPanelLeft = 16;
  constexpr int kPanelTop = 16;
  constexpr int kRowHeight = 14;
  constexpr int kIndicatorRightX = 79;
  int rightmost_text_x = -1;
  for (int y = kPanelTop; y < kPanelTop + kRowHeight; ++y) {
    for (int x = kPanelLeft + 1; x < kIndicatorRightX; ++x) {
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
  EXPECT_GE(kIndicatorRightX - rightmost_text_x - 1,
            kRequiredRightPaddingPixels);
  // Empty space around the readout remains transparent, while both rows are
  // present and the outlined glyphs contain dark shadow pixels.
  EXPECT_EQ((*pixels)[(static_cast<std::size_t>(50) * kWidth + 17) * 4 + 3],
            0);
  bool has_dark_speed_shadow = false;
  int visible_pixels_in_first_row = 0;
  int visible_pixels_in_second_row = 0;
  for (int y = 16; y < 46; ++y) {
    for (int x = 16; x < kIndicatorRightX; ++x) {
      const std::size_t index =
          (static_cast<std::size_t>(y) * kWidth + x) * 4;
      if ((*pixels)[index + 3] != 0) {
        if (y < 30) {
          ++visible_pixels_in_first_row;
        } else if (y >= 32) {
          ++visible_pixels_in_second_row;
        }
      }
      has_dark_speed_shadow =
          has_dark_speed_shadow ||
          ((*pixels)[index] == 0 && (*pixels)[index + 1] == 0 &&
           (*pixels)[index + 2] == 0 && (*pixels)[index + 3] != 0);
    }
  }
  EXPECT_TRUE(has_dark_speed_shadow);
  EXPECT_GT(visible_pixels_in_first_row, 0);
  EXPECT_GT(visible_pixels_in_second_row, 0);
}

TEST(RenderOverlayFrameRgbaTest, RightAlignsOneTwoAndThreeDigitSpeeds) {
  constexpr int kWidth = 320;
  constexpr int kHeight = 180;
  constexpr double kMetersPerSecondToMilesPerHour = 2.2369362920544;
  auto render_mph = [&](int mph) {
    const double meters_per_second =
        static_cast<double>(mph) / kMetersPerSecondToMilesPerHour;
    TelemetryData telemetry;
    telemetry.gps = {
        {.timestamp = absl::Seconds(0),
         .value = {.latitude_degrees = 30.0,
                   .longitude_degrees = -97.0,
                   .ground_speed_meters_per_second = meters_per_second}},
        {.timestamp = absl::Seconds(1),
         .value = {.latitude_degrees = 30.001,
                   .longitude_degrees = -97.0,
                   .ground_speed_meters_per_second = meters_per_second}}};
    telemetry.filtered_g_force = {
        {.timestamp = absl::Seconds(0), .value = {}},
        {.timestamp = absl::Seconds(1), .value = {}}};
    absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
    if (!overlay.ok()) {
      return absl::StatusOr<std::vector<std::uint8_t>>(overlay.status());
    }
    return RenderOverlayFrameRgba(telemetry, *overlay, 0.5, kWidth, kHeight,
                                  {SpeedUnit::kMilesPerHour});
  };
  const absl::StatusOr<std::vector<std::uint8_t>> one_digit = render_mph(9);
  const absl::StatusOr<std::vector<std::uint8_t>> two_digits = render_mph(99);
  const absl::StatusOr<std::vector<std::uint8_t>> three_digits =
      render_mph(999);
  ASSERT_TRUE(one_digit.ok()) << one_digit.status();
  ASSERT_TRUE(two_digits.ok()) << two_digits.status();
  ASSERT_TRUE(three_digits.ok()) << three_digits.status();

  auto rightmost_number_x = [&](const std::vector<std::uint8_t>& pixels) {
    int result = -1;
    // Inspect only the first row's number field, excluding the MPH text.
    for (int y = 16; y < 30; ++y) {
      for (int x = 16; x < 58; ++x) {
        const std::size_t red_index =
            (static_cast<std::size_t>(y) * kWidth + x) * 4;
        if (pixels[red_index] > 220 && pixels[red_index + 3] > 240) {
          result = std::max(result, x);
        }
      }
    }
    return result;
  };
  const int one_digit_right = rightmost_number_x(*one_digit);
  const int two_digit_right = rightmost_number_x(*two_digits);
  const int three_digit_right = rightmost_number_x(*three_digits);
  ASSERT_GE(one_digit_right, 0);
  EXPECT_EQ(one_digit_right, two_digit_right);
  EXPECT_EQ(two_digit_right, three_digit_right);
}

TEST(RenderOverlayFrameRgbaTest,
     DrawsNarrowBlueRouteOverThickWhiteTrackWithoutPanel) {
  TelemetryData telemetry;
  telemetry.gps = {
      {.timestamp = absl::Seconds(0),
       .value = {.latitude_degrees = 30.0,
                 .longitude_degrees = -97.0,
                 .ground_speed_meters_per_second = 10}},
      {.timestamp = absl::Seconds(1),
       .value = {.latitude_degrees = 30.001,
                 .longitude_degrees = -97.0,
                 .ground_speed_meters_per_second = 10}}};
  telemetry.filtered_g_force = {
      {.timestamp = absl::Seconds(0), .value = {}},
      {.timestamp = absl::Seconds(1), .value = {}}};
  const absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
  ASSERT_TRUE(overlay.ok()) << overlay.status();
  constexpr int kWidth = 320;
  constexpr int kHeight = 180;

  const absl::StatusOr<std::vector<std::uint8_t>> pixels =
      RenderOverlayFrameRgba(telemetry, *overlay, 1.0, kWidth, kHeight,
                             {SpeedUnit::kKilometersPerHour});

  ASSERT_TRUE(pixels.ok()) << pixels.status();
  auto pixel = [&](int x, int y, int component) {
    return (*pixels)[(static_cast<std::size_t>(y) * kWidth + x) * 4 +
                     component];
  };
  // The 75x75 track region begins at (229, 16). Its unused corner remains
  // transparent because there is no track panel.
  EXPECT_EQ(pixel(229, 16, 3), 0);
  // The vertical test route is centered at x=266. Blue occupies its narrow
  // center, while the thicker white base remains visible at the edge.
  EXPECT_EQ(pixel(266, 53, 0), 30);
  EXPECT_EQ(pixel(266, 53, 1), 145);
  EXPECT_EQ(pixel(266, 53, 2), 255);
  EXPECT_EQ(pixel(268, 53, 0), 230);
  EXPECT_EQ(pixel(268, 53, 1), 230);
  EXPECT_EQ(pixel(268, 53, 2), 230);
  EXPECT_EQ(pixel(270, 53, 0), 0);
  EXPECT_EQ(pixel(270, 53, 1), 0);
  EXPECT_EQ(pixel(270, 53, 2), 0);
  EXPECT_GT(pixel(270, 53, 3), 0);
  // This route heads north, so the red arrow tip extends upward from the
  // current track position at (266, 24).
  EXPECT_EQ(pixel(266, 17, 0), 255);
  EXPECT_EQ(pixel(266, 17, 1), 70);
  EXPECT_EQ(pixel(266, 17, 2), 60);
  // The G-force panel is absent. Its white outer ring has a black outline.
  EXPECT_EQ(pixel(16, 70, 3), 0);
  EXPECT_GE(pixel(76, 119, 0), 250);
  EXPECT_GE(pixel(76, 119, 1), 250);
  EXPECT_GE(pixel(76, 119, 2), 250);
  EXPECT_EQ(pixel(79, 119, 0), 0);
  EXPECT_EQ(pixel(79, 119, 1), 0);
  EXPECT_EQ(pixel(79, 119, 2), 0);
  EXPECT_GT(pixel(79, 119, 3), 0);
}

TEST(RenderOverlayFrameRgbaTest, MapsOneGToInnerRingAndCapsDotAtOnePointTwoG) {
  constexpr int kWidth = 320;
  constexpr int kHeight = 180;
  auto render_lateral_g = [&](double lateral_g) {
    TelemetryData telemetry;
    telemetry.gps = {
        {.timestamp = absl::Seconds(0),
         .value = {.latitude_degrees = 30.0,
                   .longitude_degrees = -97.0,
                   .ground_speed_meters_per_second = 10}},
        {.timestamp = absl::Seconds(1),
         .value = {.latitude_degrees = 30.001,
                   .longitude_degrees = -97.0,
                   .ground_speed_meters_per_second = 10}}};
    telemetry.filtered_g_force = {
        {.timestamp = absl::Seconds(0), .value = {.lateral_g = lateral_g}},
        {.timestamp = absl::Seconds(1), .value = {.lateral_g = lateral_g}}};
    absl::StatusOr<OverlayData> overlay = BuildOverlayData(telemetry);
    if (!overlay.ok()) {
      return absl::StatusOr<std::vector<std::uint8_t>>(overlay.status());
    }
    return RenderOverlayFrameRgba(telemetry, *overlay, 0.5, kWidth, kHeight,
                                  {});
  };
  const absl::StatusOr<std::vector<std::uint8_t>> one_g =
      render_lateral_g(1.0);
  const absl::StatusOr<std::vector<std::uint8_t>> one_point_two_g =
      render_lateral_g(1.2);
  const absl::StatusOr<std::vector<std::uint8_t>> beyond_scale =
      render_lateral_g(2.5);
  ASSERT_TRUE(one_g.ok()) << one_g.status();
  ASSERT_TRUE(one_point_two_g.ok()) << one_point_two_g.status();
  ASSERT_TRUE(beyond_scale.ok()) << beyond_scale.status();
  auto rightmost_red_x = [](const std::vector<std::uint8_t>& pixels) {
    int result = -1;
    for (int x = 0; x < 100; ++x) {
      const std::size_t index =
          (static_cast<std::size_t>(119) * kWidth + x) * 4;
      if (pixels[index] == 255 && pixels[index + 1] == 70 &&
          pixels[index + 2] == 60 && pixels[index + 3] == 255) {
        result = x;
      }
    }
    return result;
  };
  // The 30-pixel outer radius represents 1.2 g, so the 1 g ring is 25
  // pixels from the center at (46, 119). The dot itself has an 8-pixel
  // radius, making its right edge 79 pixels at 1 g and 84 pixels at 1.2 g.
  EXPECT_EQ(rightmost_red_x(*one_g), 79);
  EXPECT_EQ(rightmost_red_x(*one_point_two_g), 84);
  EXPECT_EQ(rightmost_red_x(*beyond_scale), 84);
}

}  // namespace
}  // namespace racevideo
