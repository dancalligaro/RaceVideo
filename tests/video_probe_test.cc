#include "ffmpeg/video_probe.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(ParseFfprobeOutputTest, ReadsVideoAudioAndFractionalFrameRate) {
  constexpr char kOutput[] = R"(
streams.stream.0.codec_type="video"
streams.stream.0.width=1920
streams.stream.0.height=1080
streams.stream.0.r_frame_rate="30000/1001"
streams.stream.0.avg_frame_rate="30000/1001"
streams.stream.1.codec_type="audio"
format.duration="532.532000"
)";

  const absl::StatusOr<VideoInfo> info = ParseFfprobeOutput(kOutput);

  ASSERT_TRUE(info.ok()) << info.status();
  EXPECT_EQ(info->width, 1920);
  EXPECT_EQ(info->height, 1080);
  EXPECT_NEAR(info->frames_per_second, 29.97002997, 1e-8);
  EXPECT_DOUBLE_EQ(info->duration_seconds, 532.532);
  EXPECT_TRUE(info->has_audio);
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
