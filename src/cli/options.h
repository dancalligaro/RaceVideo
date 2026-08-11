#ifndef RACEVIDEO_CLI_OPTIONS_H_
#define RACEVIDEO_CLI_OPTIONS_H_

#include <filesystem>

#include "absl/status/statusor.h"

namespace racevideo {

struct Options {
  std::filesystem::path input_path;
};

absl::StatusOr<Options> ParseOptions(int argc, char* argv[]);

}  // namespace racevideo

#endif  // RACEVIDEO_CLI_OPTIONS_H_

