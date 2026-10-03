#include "session_commands.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "gtest/gtest.h"

namespace racevideo {
namespace {

class SessionCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
        ("racevideo_commands_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    ASSERT_FALSE(error);
    options_.folder = directory_;
    options_.defaults = directory_ / "defaults.txt";
    options_.executable = "./racevideo";
    WriteDefaults("imu_axis_order=ZXY\nspeed_unit=kmh,mph\n");
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(directory_, error);
  }
  void Touch(const std::string& name) {
    std::ofstream file(directory_ / name);
    ASSERT_TRUE(file);
  }
  void WriteDefaults(const std::string& text) {
    std::ofstream file(options_.defaults, std::ios::binary);
    file << text;
    ASSERT_TRUE(file);
  }
  std::filesystem::path directory_;
  SessionCommandOptions options_;
};

TEST_F(SessionCommandsTest, GroupsSessionsAndSortsChapters) {
  Touch("GP029595.MP4");
  Touch("GOPR9596.MP4");
  Touch("GP019595.MP4");
  Touch("GOPR9595.MP4");
  Touch("unrelated.mp4");
  const auto script = GenerateSessionCommands(options_);
  ASSERT_TRUE(script.ok()) << script.status();
  const auto first = script->find("--input=" + (directory_ / "GOPR9595.MP4").string());
  const auto second = script->find("--input=" + (directory_ / "GP019595.MP4").string());
  const auto third = script->find("--input=" + (directory_ / "GP029595.MP4").string());
  EXPECT_LT(first, second);
  EXPECT_LT(second, third);
  EXPECT_LT(third, script->find("--input=" + (directory_ / "GOPR9596.MP4").string()));
  EXPECT_NE(script->find("--output_video=overlay-GOPR9595.mp4"), std::string::npos);
  EXPECT_NE(script->find("--output_video=overlay-GOPR9596.mp4"), std::string::npos);
  EXPECT_EQ(script->find("unrelated.mp4"), std::string::npos);
}

TEST_F(SessionCommandsTest, SupportsModernNamesAndLowercaseExtensions) {
  Touch("gh019595.mp4");
  Touch("GH029595.MP4");
  Touch("GX019595.MP4");
  const auto script = GenerateSessionCommands(options_);
  ASSERT_TRUE(script.ok()) << script.status();
  EXPECT_NE(script->find("overlay-GH019595.mp4"), std::string::npos);
  EXPECT_NE(script->find("overlay-GX019595.mp4"), std::string::npos);
}

TEST_F(SessionCommandsTest, RejectsMissingFirstAndIntermediateChapters) {
  Touch("GP029595.MP4");
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
  Touch("GOPR9595.MP4");
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
  Touch("GP019595.MP4");
  EXPECT_TRUE(GenerateSessionCommands(options_).ok());
}

TEST_F(SessionCommandsTest, RejectsDuplicateCaseInsensitiveChapters) {
  Touch("GOPR9595.MP4");
  Touch("gopr9595.mp4");
  std::error_code error;
  const bool same_file = std::filesystem::equivalent(
      directory_ / "GOPR9595.MP4", directory_ / "gopr9595.mp4", error);
  ASSERT_FALSE(error) << error.message();
  if (same_file) {
    GTEST_SKIP() << "Requires a filesystem that preserves distinct filenames "
                    "differing only by case";
  }
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
}

TEST_F(SessionCommandsTest, ReadsBomCommentsCrLfAndQuotedValues) {
  Touch("GOPR9595.MP4");
  WriteDefaults("\xef\xbb\xbf# settings\r\n\r\n --imu_axis_order = \"ZXY\" \r\n"
                "speed_unit='kmh,mph'\r\n");
  const auto script = GenerateSessionCommands(options_);
  ASSERT_TRUE(script.ok()) << script.status();
  EXPECT_NE(script->find("'--imu_axis_order=ZXY'"), std::string::npos);
  EXPECT_NE(script->find("'--speed_unit=kmh,mph'"), std::string::npos);
}

TEST_F(SessionCommandsTest, RejectsMalformedDuplicateAndReservedDefaults) {
  Touch("GOPR9595.MP4");
  for (const char* text : {"imu_axis_order ZXY\n",
                                 "imu_axis_order=ZXY\nimu_axis_order=XYZ\n",
                                 "imu_axis_order=ZXY\ninput=x.mp4\n",
                                 "imu_axis_order=ZXY\noutput_video=x.mp4\n",
                                 "imu_axis_order=ZXY\ninput_list=x.txt\n",
                                 "video_encoder=nvidia\n"}) {
    WriteDefaults(text);
    EXPECT_FALSE(GenerateSessionCommands(options_).ok()) << text;
  }
}

TEST_F(SessionCommandsTest, QuotesShellMetacharactersAndExecutable) {
  Touch("GOPR9595.MP4");
  options_.output_prefix = "driver's $day; ";
  options_.executable = "my tools/racevideo";
  const auto bash = GenerateSessionCommands(options_);
  ASSERT_TRUE(bash.ok()) << bash.status();
  EXPECT_NE(bash->find("'my tools/racevideo' \\\n"), std::string::npos);
  EXPECT_NE(bash->find("'--output_video=driver'\\''s $day; GOPR9595.mp4'"),
            std::string::npos);
  options_.powershell = true;
  const auto powershell = GenerateSessionCommands(options_);
  ASSERT_TRUE(powershell.ok()) << powershell.status();
  EXPECT_NE(powershell->find("& 'my tools/racevideo' `\n"), std::string::npos);
  EXPECT_NE(powershell->find("'--output_video=driver''s $day; GOPR9595.mp4'"),
            std::string::npos);
  EXPECT_NE(powershell->find("if ($LASTEXITCODE -ne 0)"), std::string::npos);
}

TEST_F(SessionCommandsTest, EmptyAndMissingFoldersReturnErrors) {
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
  options_.folder = directory_ / "missing";
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
}

TEST_F(SessionCommandsTest, ReadsGeneratorDefaultsAndExpandsHome) {
  Touch("GOPR9595.MP4");
  options_.executable.clear();
  WriteDefaults("racevideo=~/Racevideo/bin/linux/racevideo\n"
                "output_prefix=\"~/racevideo_output/full-\"\n"
                "imu_axis_order=ZXY\n");
#ifdef _WIN32
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  ASSERT_NE(home, nullptr);
  const auto script = GenerateSessionCommands(options_);
  ASSERT_TRUE(script.ok()) << script.status();
  EXPECT_NE(script->find((std::filesystem::path(home) /
                         "Racevideo/bin/linux/racevideo").string()),
            std::string::npos);
  EXPECT_NE(script->find("--output_video=" +
                        (std::filesystem::path(home) /
                         "racevideo_output/full-GOPR9595.mp4").string()),
            std::string::npos);
  EXPECT_EQ(script->find("--racevideo="), std::string::npos);
  EXPECT_EQ(script->find("--output_prefix="), std::string::npos);
  EXPECT_EQ(script->find('~'), std::string::npos);
}

TEST_F(SessionCommandsTest, CommandLineOverridesGeneratorDefaults) {
  Touch("GOPR9595.MP4");
  WriteDefaults("racevideo=default-binary\noutput_prefix=default-\n"
                "imu_axis_order=ZXY\n");
  options_.executable = "my tools/racevideo";
  options_.output_prefix = "chosen-";
  const auto script = GenerateSessionCommands(options_);
  ASSERT_TRUE(script.ok()) << script.status();
  EXPECT_NE(script->find("'my tools/racevideo'"), std::string::npos);
  EXPECT_NE(script->find("--output_video=chosen-GOPR9595.mp4"), std::string::npos);
  EXPECT_EQ(script->find("default-"), std::string::npos);
  options_.output_prefix = "";
  const auto no_prefix = GenerateSessionCommands(options_);
  ASSERT_TRUE(no_prefix.ok()) << no_prefix.status();
  EXPECT_NE(no_prefix->find("--output_video=GOPR9595.mp4"), std::string::npos);
}

TEST_F(SessionCommandsTest, RejectsEmptyExecutableAndUnsupportedHomeSyntax) {
  Touch("GOPR9595.MP4");
  options_.executable.clear();
  WriteDefaults("racevideo=\nimu_axis_order=ZXY\n");
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
  WriteDefaults("racevideo=~someone/racevideo\nimu_axis_order=ZXY\n");
  EXPECT_FALSE(GenerateSessionCommands(options_).ok());
}

}  // namespace
}  // namespace racevideo
