#include "ffmpeg/process.h"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace racevideo {
namespace {

const std::filesystem::path kHelper(RACEVIDEO_PROCESS_HELPER);

TEST(ProcessTest, PreservesLiteralArguments) {
  const std::vector<std::string> arguments = {
      "arguments", "two words", "quote\"here", "trailing\\", "$(echo nope);*"};
  const auto result = RunProcessAndCaptureOutput(kHelper, arguments);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->exit_code, 0u);
  std::string expected;
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    expected +=
        std::to_string(arguments[index].size()) + ":" + arguments[index] + "\n";
  }
  EXPECT_EQ(result->output, expected);
}

TEST(ProcessTest, CapturesNonzeroExitAndStderr) {
  const auto result = RunProcessAndCaptureOutput(kHelper, {"exit"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->exit_code, 23u);
  EXPECT_EQ(result->output, "expected diagnostic");
}

TEST(ProcessTest, ReportsMissingExecutable) {
  EXPECT_FALSE(RunProcessAndCaptureOutput(kHelper / "missing", {}).ok());
}

TEST(ProcessTest, DrainsOutputBeyondCaptureLimit) {
  const auto result = RunProcessAndCaptureOutput(kHelper, {"flood"});
  EXPECT_EQ(result.status().code(), absl::StatusCode::kResourceExhausted);
}

TEST(ProcessTest, StreamsBinaryInputWhileDrainingOutput) {
  std::vector<std::uint8_t> bytes(2 * 1024 * 1024);
  unsigned long long sum = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>(index % 256);
    sum += bytes[index];
  }
  const auto result = RunProcessWithInput(
      kHelper, {"duplex"}, [&](const ByteSink& sink) { return sink(bytes); });
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->exit_code, 0u);
  EXPECT_TRUE(result->output.ends_with("bytes=" + std::to_string(bytes.size()) +
                                       " sum=" + std::to_string(sum)));
}

TEST(ProcessTest, EarlyChildExitReturnsErrorInsteadOfCrashing) {
  const std::vector<std::uint8_t> bytes(2 * 1024 * 1024);
  const auto result = RunProcessWithInput(
      kHelper, {"exit"}, [&](const ByteSink& sink) { return sink(bytes); });
  EXPECT_FALSE(result.ok());
}

#ifndef _WIN32
TEST(ProcessTest, PreservesEmptyAndUtf8Arguments) {
  const auto result =
      RunProcessAndCaptureOutput(kHelper, {"arguments", "", "caf\xc3\xa9"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->output, "0:\n5:caf\xc3\xa9\n");
}

TEST(ProcessTest, CaptureOnlyClosesStdin) {
  const auto result = RunProcessAndCaptureOutput(kHelper, {"duplex"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->exit_code, 0u);
  EXPECT_TRUE(result->output.ends_with("bytes=0 sum=0"));
}

TEST(ProcessTest, ReportsSignalTermination) {
  const auto result = RunProcessAndCaptureOutput(kHelper, {"signal"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->exit_code, static_cast<unsigned long>(128 + SIGTERM));
}

TEST(ProcessTest, ProducerFailureTerminatesAndReapsChild) {
  const auto result = RunProcessWithInput(
      kHelper, {"wait"},
      [](const ByteSink&) { return absl::CancelledError("producer stopped"); });
  EXPECT_EQ(result.status().code(), absl::StatusCode::kCancelled);
}

TEST(ProcessTest, RejectsEmbeddedNulArguments) {
  EXPECT_EQ(RunProcessAndCaptureOutput(kHelper, {std::string("a\0b", 3)})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ProcessTest, FindsOnlyExecutableFilesInAbsolutePathDirectories) {
  const auto nonexecutable = kHelper.parent_path() / "racevideo-nonexecutable";
  {
    std::ofstream file(nonexecutable);
    ASSERT_TRUE(file);
    file << "not an executable";
  }
  const char* original = std::getenv("PATH");
  const bool had_path = original != nullptr;
  const std::string saved = had_path ? original : "";
  // The helper's directory is controlled by the build and is an absolute path.
  const std::string path = ":relative:" + kHelper.parent_path().string();
  ASSERT_EQ(setenv("PATH", path.c_str(), 1), 0);
  const auto found = FindExecutableOnPath(kHelper.filename().string());
  const auto directory = FindExecutableOnPath(".");
  const auto text_file =
      FindExecutableOnPath(nonexecutable.filename().string());
  const auto invalid = FindExecutableOnPath("../tool");
  if (had_path) {
    EXPECT_EQ(setenv("PATH", saved.c_str(), 1), 0);
  } else {
    EXPECT_EQ(unsetenv("PATH"), 0);
  }
  std::error_code error;
  std::filesystem::remove(nonexecutable, error);
  EXPECT_FALSE(error);
  ASSERT_TRUE(found.ok()) << found.status();
  EXPECT_EQ(*found, std::filesystem::canonical(kHelper));
  EXPECT_FALSE(directory.ok());
  EXPECT_FALSE(text_file.ok());
  EXPECT_EQ(invalid.status().code(), absl::StatusCode::kInvalidArgument);
}
#endif

}  // namespace
}  // namespace racevideo
