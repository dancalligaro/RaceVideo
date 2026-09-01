#ifndef RACEVIDEO_FFMPEG_PROCESS_H_
#define RACEVIDEO_FFMPEG_PROCESS_H_

#include <filesystem>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace racevideo {

struct ProcessResult {
  unsigned long exit_code;
  std::string output;
};

using ByteSink =
    std::function<absl::Status(std::span<const std::uint8_t> bytes)>;
using InputProducer = std::function<absl::Status(const ByteSink& sink)>;

absl::StatusOr<std::filesystem::path> FindExecutableOnPath(
    std::string_view executable_name);
absl::StatusOr<ProcessResult> RunProcessAndCaptureOutput(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments);
absl::StatusOr<ProcessResult> RunProcessWithInput(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, const InputProducer& producer);

}  // namespace racevideo

#endif  // RACEVIDEO_FFMPEG_PROCESS_H_
