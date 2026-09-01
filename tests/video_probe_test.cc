#include "ffmpeg/video_probe.h"

#include "ffmpeg/video_encoder.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(DetermineOutputDimensionsTest, PreservesOrScalesAspectRatioToEvenHeight) {
  const VideoInfo video = {.width = 1920,
                           .height = 1080,
                           .frames_per_second = 30,
                           .duration_seconds = 10,
                           .has_audio = true};

  EXPECT_EQ(*DetermineOutputDimensions(video, 0),
            (VideoDimensions{1920, 1080}));
  EXPECT_EQ(*DetermineOutputDimensions(video, 400),
            (VideoDimensions{400, 226}));
  EXPECT_EQ(*DetermineOutputDimensions(video, 640),
            (VideoDimensions{640, 360}));
  EXPECT_EQ(DetermineOutputDimensions(video, 401).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(DetermineOutputDimensions(video, 2000).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ParseFfprobeOutputTest, ReadsVideoAudioAndFractionalFrameRate) {
  constexpr char kOutput[] = R"(
streams.stream.0.codec_type="video"
streams.stream.0.codec_name="h264"
streams.stream.0.width=1920
streams.stream.0.height=1080
streams.stream.0.pix_fmt="yuv420p"
streams.stream.0.time_base="1/60000"
streams.stream.0.r_frame_rate="30000/1001"
streams.stream.0.avg_frame_rate="30000/1001"
streams.stream.1.codec_type="audio"
streams.stream.1.codec_name="aac"
streams.stream.1.sample_rate="48000"
streams.stream.1.channels=2
streams.stream.1.channel_layout="stereo"
streams.stream.1.time_base="1/48000"
format.duration="532.532000"
)";

  const absl::StatusOr<VideoInfo> info = ParseFfprobeOutput(kOutput);

  ASSERT_TRUE(info.ok()) << info.status();
  EXPECT_EQ(info->width, 1920);
  EXPECT_EQ(info->height, 1080);
  EXPECT_NEAR(info->frames_per_second, 29.97002997, 1e-8);
  EXPECT_DOUBLE_EQ(info->duration_seconds, 532.532);
  EXPECT_TRUE(info->has_audio);
  EXPECT_EQ(info->video_codec, "h264");
  EXPECT_EQ(info->audio_codec, "aac");
}

TEST(ValidateCompatibleVideoTest, IdentifiesMismatchedProperty) {
  VideoInfo expected = {.width = 1920,
                        .height = 1080,
                        .frames_per_second = 60,
                        .duration_seconds = 300,
                        .has_audio = true,
                        .video_codec = "h264",
                        .pixel_format = "yuv420p",
                        .video_time_base = "1/60000",
                        .rotation = "0",
                        .color_space = "bt709",
                        .color_transfer = "bt709",
                        .color_primaries = "bt709",
                        .audio_codec = "aac",
                        .audio_sample_rate = "48000",
                        .audio_channels = 2,
                        .audio_channel_layout = "stereo",
                        .audio_time_base = "1/48000"};
  VideoInfo candidate = expected;
  candidate.duration_seconds = 250;
  EXPECT_TRUE(ValidateCompatibleVideo(expected, candidate).ok());
  candidate.width = 1280;
  const absl::Status status = ValidateCompatibleVideo(expected, candidate);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("dimensions"), std::string_view::npos);
}

TEST(ParseFfprobeOutputTest, FallsBackToRealFrameRate) {
  constexpr char kOutput[] = R"(
streams.stream.0.codec_type="video"
streams.stream.0.width=1280
streams.stream.0.height=720
streams.stream.0.r_frame_rate="60/1"
streams.stream.0.avg_frame_rate="0/0"
format.duration="10.0"
)";

  const absl::StatusOr<VideoInfo> info = ParseFfprobeOutput(kOutput);

  ASSERT_TRUE(info.ok()) << info.status();
  EXPECT_DOUBLE_EQ(info->frames_per_second, 60.0);
  EXPECT_FALSE(info->has_audio);
}

TEST(ParseFfprobeOutputTest, RejectsMissingVideoDimensions) {
  constexpr char kOutput[] = R"(
streams.stream.0.codec_type="video"
streams.stream.0.avg_frame_rate="30/1"
format.duration="10.0"
)";

  const absl::StatusOr<VideoInfo> info = ParseFfprobeOutput(kOutput);

  EXPECT_EQ(info.status().code(), absl::StatusCode::kDataLoss);
}

}  // namespace
}  // namespace racevideo
