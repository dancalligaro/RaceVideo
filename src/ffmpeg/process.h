#ifndef RACEVIDEO_FFMPEG_PROCESS_H_
#define RACEVIDEO_FFMPEG_PROCESS_H_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace racevideo {

struct ProcessResult {
  unsigned long exit_code;
  std::string output;
};

absl::StatusOr<std::filesystem::path> FindExecutableOnPath(
    std::string_view executable_name);
absl::StatusOr<ProcessResult> RunProcessAndCaptureOutput(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_PROCESS_H_
