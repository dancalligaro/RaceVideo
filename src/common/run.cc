#include "common/run.h"

#include <filesystem>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace racevideo {

absl::Status Run(const Options& options) {
  if (!std::filesystem::exists(options.input_path)) {
    return absl::NotFoundError(
        absl::StrCat("input file not found: ", options.input_path.string()));
  }
  if (!std::filesystem::is_regular_file(options.input_path)) {
    return absl::InvalidArgumentError(
        absl::StrCat("input is not a file: ", options.input_path.string()));
  }

  return absl::UnimplementedError(
      "GoPro telemetry extraction is the next implementation milestone");
}

}  // namespace racevideo

