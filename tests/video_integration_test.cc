#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "ffmpeg/process.h"
#include "ffmpeg/video_encoder.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

std::string Utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

struct VideoEncodeCase {
  bool multiple_chapters;
  VideoEncoder encoder;
};

class VideoIntegrationTest : public testing::TestWithParam<VideoEncodeCase> {
 protected:
  void SetUp() override {
    directory_ =
        std::filesystem::temp_directory_path() /
        ("racevideo-integration-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    ASSERT_TRUE(std::filesystem::create_directory(directory_, error)) << error;
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(directory_, error);
    EXPECT_FALSE(error) << error;
  }
  std::filesystem::path directory_;
};

TEST_P(VideoIntegrationTest, EncodesOverlayWithAudioAndLiteralChapterPaths) {
  const VideoEncodeCase test_case = GetParam();
  const auto ffmpeg = FindExecutableOnPath("ffmpeg");
  ASSERT_TRUE(ffmpeg.ok()) << ffmpeg.status();
  ASSERT_TRUE(FindExecutableOnPath("ffprobe").ok());
  std::u8string filename = u8"chapter 'caf\u00e9'";
#ifndef _WIN32
  filename += u8"\\literal";
#endif
  const auto first = directory_ / std::filesystem::path(filename + u8".mp4");
  const auto second = directory_ / "chapter two.mp4";
  const auto source = RunProcessAndCaptureOutput(
      *ffmpeg,
      {"-hide_banner", "-loglevel",
       "error",        "-nostdin",
       "-f",           "lavfi",
       "-i",           "testsrc2=size=320x180:rate=10:duration=1",
       "-f",           "lavfi",
       "-i",           "sine=frequency=440:sample_rate=48000:duration=1",
       "-c:v",         "libx264",
       "-threads",     "1",
       "-pix_fmt",     "yuv420p",
       "-c:a",         "aac",
       "-shortest",    "-n",
       Utf8(first)});
  ASSERT_TRUE(source.ok()) << source.status();
  ASSERT_EQ(source->exit_code, 0u) << source->output;
  const auto video = ProbeVideo(first);
  ASSERT_TRUE(video.ok()) << video.status();
  std::vector<VideoChapter> chapters{{first, video->duration_seconds}};
  if (test_case.multiple_chapters) {
    std::error_code error;
    ASSERT_TRUE(std::filesystem::copy_file(first, second, error)) << error;
    chapters.push_back({second, video->duration_seconds});
  }

  TelemetryData telemetry;
  telemetry.gps = {{.timestamp = absl::Seconds(0),
                    .value = {.latitude_degrees = 30.0,
                              .longitude_degrees = -97.0,
                              .ground_speed_meters_per_second = 10}},
                   {.timestamp = absl::Seconds(2),
                    .value = {.latitude_degrees = 30.001,
                              .longitude_degrees = -97.0,
                              .ground_speed_meters_per_second = 11}}};
  telemetry.filtered_g_force = {{.timestamp = absl::Seconds(0), .value = {}},
                                {.timestamp = absl::Seconds(2), .value = {}}};
  const auto overlay = BuildOverlayData(telemetry);
  ASSERT_TRUE(overlay.ok()) << overlay.status();
  const VideoEncodeOptions options{
      .chapters = chapters,
      .output_path = directory_ / "overlay result.mp4",
      .start_seconds = test_case.multiple_chapters ? 0.5 : 0.0,
      .duration_seconds = 1.0,
      .output_width = 160,
      .video_encoder = test_case.encoder,
      .video_pipeline = VideoPipeline::kSoftware,
      .speed_units = {SpeedUnit::kKilometersPerHour}};
  const auto status = EncodeOverlayVideo(telemetry, *overlay, *video, options);
  ASSERT_TRUE(status.ok()) << status;
  const auto result = ProbeVideo(options.output_path);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->width, 160);
  EXPECT_EQ(result->height, 90);
  EXPECT_EQ(result->video_codec, "h264");
  EXPECT_EQ(result->audio_codec, "aac");
  EXPECT_TRUE(result->has_audio);
  EXPECT_NEAR(result->duration_seconds, 1.0, 0.15);
  EXPECT_NEAR(result->frames_per_second, 10.0, 0.01);
  const auto previous_size = std::filesystem::file_size(options.output_path);
  EXPECT_FALSE(EncodeOverlayVideo(telemetry, *overlay, *video, options).ok());
  EXPECT_EQ(std::filesystem::file_size(options.output_path), previous_size);
}

INSTANTIATE_TEST_SUITE_P(
    Software, VideoIntegrationTest,
    testing::Values(VideoEncodeCase{false, VideoEncoder::kSoftware},
                    VideoEncodeCase{true, VideoEncoder::kSoftware}),
    [](const testing::TestParamInfo<VideoEncodeCase>& info) {
      return info.param.multiple_chapters ? "MultipleChapters"
                                          : "SingleChapter";
    });

#ifdef __APPLE__
INSTANTIATE_TEST_SUITE_P(
    VideoToolbox, VideoIntegrationTest,
    testing::Values(
        VideoEncodeCase{false, VideoEncoder::kVideoToolbox}),
    [](const testing::TestParamInfo<VideoEncodeCase>&) {
      return "SingleChapter";
    });
#endif

}  // namespace
}  // namespace racevideo
