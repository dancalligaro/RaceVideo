#include "cli/options.h"

#include <string>
#include <vector>

#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(ParseOptionsTest, RequiresInput) {
  absl::FlagSaver flag_saver;
  char program[] = "racevideo";
  char* argv[] = {program};

  const absl::StatusOr<Options> options = ParseOptions(1, argv);

  EXPECT_EQ(options.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParseOptionsTest, PreservesRepeatedInputOrder) {
  absl::FlagSaver flag_saver;
  char program[] = "racevideo";
  char input_one[] = "--input=GX010001.MP4";
  char input_two_flag[] = "--input";
  char input_two_path[] = "GX020001.MP4";
  char output[] = "--output_video=combined.mp4";
  char axes[] = "--imu_axis_order=ZXY";
  char* argv[] = {program, input_one, input_two_flag, input_two_path, output,
                  axes};

  const absl::StatusOr<Options> options = ParseOptions(6, argv);

  ASSERT_TRUE(options.ok()) << options.status();
  EXPECT_EQ(options->input_paths,
            std::vector<std::filesystem::path>(
                {"GX010001.MP4", "GX020001.MP4"}));
}

TEST(ParseOptionsTest, SelectsVaapiEncoderAndCustomDevice) {
  absl::FlagSaver flag_saver;
  char program[] = "racevideo";
  char input[] = "--input=video.mp4";
  char output[] = "--output_video=overlay.mp4";
  char axes[] = "--imu_axis_order=ZXY";
  char encoder[] = "--video_encoder=vaapi";
  char device[] = "--vaapi_device=/dev/dri/renderD129";
  char* argv[] = {program, input, output, axes, encoder, device};

  const auto options = ParseOptions(6, argv);

  ASSERT_TRUE(options.ok()) << options.status();
  EXPECT_EQ(options->video_encoder, VideoEncoder::kVaapi);
  EXPECT_EQ(options->video_pipeline, VideoPipeline::kSoftware);
  EXPECT_EQ(options->vaapi_device, "/dev/dri/renderD129");
}

TEST(ParseOptionsTest, MultipleInputsRequireRendering) {
  absl::FlagSaver flag_saver;
  char program[] = "racevideo";
  char input_one[] = "--input=GX010001.MP4";
  char input_two[] = "--input=GX020001.MP4";
  char* argv[] = {program, input_one, input_two};

  const absl::StatusOr<Options> options = ParseOptions(3, argv);

  EXPECT_EQ(options.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(options.status().message().find("multiple --input values"),
            std::string::npos);
}

TEST(ParseSpeedUnitsTest, SupportsHiddenAndEveryDocumentedOrdering) {
  EXPECT_EQ(*ParseSpeedUnits(""), std::vector<SpeedUnit>({}));
  EXPECT_EQ(*ParseSpeedUnits("kmh"),
            std::vector<SpeedUnit>({SpeedUnit::kKilometersPerHour}));
  EXPECT_EQ(*ParseSpeedUnits("mph"),
            std::vector<SpeedUnit>({SpeedUnit::kMilesPerHour}));
  EXPECT_EQ(*ParseSpeedUnits("kmh,mph"),
            std::vector<SpeedUnit>({SpeedUnit::kKilometersPerHour,
                                    SpeedUnit::kMilesPerHour}));
  EXPECT_EQ(*ParseSpeedUnits("mph,kmh"),
            std::vector<SpeedUnit>({SpeedUnit::kMilesPerHour,
                                    SpeedUnit::kKilometersPerHour}));
}

TEST(ParseSpeedUnitsTest, IsCaseInsensitiveAndRejectsOtherLists) {
  EXPECT_EQ(*ParseSpeedUnits("MPH,KMH"),
            std::vector<SpeedUnit>({SpeedUnit::kMilesPerHour,
                                    SpeedUnit::kKilometersPerHour}));
  EXPECT_EQ(ParseSpeedUnits("kmh,kmh").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseSpeedUnits("mph,").status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ParseVideoEncoderTest, SupportsDocumentedEncoders) {
  EXPECT_EQ(*ParseVideoEncoder("software"), VideoEncoder::kSoftware);
  EXPECT_EQ(*ParseVideoEncoder("NVIDIA"), VideoEncoder::kNvidia);
  EXPECT_EQ(*ParseVideoEncoder("VAAPI"), VideoEncoder::kVaapi);
  EXPECT_EQ(*ParseVideoEncoder("VideoToolbox"),
            VideoEncoder::kVideoToolbox);
  EXPECT_EQ(ParseVideoEncoder("automatic").status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ParseVideoPipelineTest, SupportsSoftwareAndNvidia) {
  EXPECT_EQ(*ParseVideoPipeline("software"), VideoPipeline::kSoftware);
  EXPECT_EQ(*ParseVideoPipeline("NVIDIA"), VideoPipeline::kNvidia);
  EXPECT_EQ(ParseVideoPipeline("automatic").status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace racevideo
