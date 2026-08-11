#include "cli/options.h"

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"

ABSL_FLAG(std::string, input, "", "Path to the input GoPro MP4 file");

namespace racevideo {

absl::StatusOr<Options> ParseOptions(int argc, char* argv[]) {
  std::vector<char*> positional_arguments = absl::ParseCommandLine(argc, argv);
  if (positional_arguments.size() > 1) {
    return absl::InvalidArgumentError(
        "positional arguments are not supported; use --input=<path>");
  }

  const std::string input = absl::GetFlag(FLAGS_input);
  if (input.empty()) {
    return absl::InvalidArgumentError("--input is required");
  }

  return Options{.input_path = input};
}

}  // namespace racevideo

