#ifndef RACEVIDEO_TOOLS_SESSION_COMMANDS_H_
#define RACEVIDEO_TOOLS_SESSION_COMMANDS_H_

#include <filesystem>
#include <optional>
#include <string>

#include "absl/status/statusor.h"

namespace racevideo {

struct SessionCommandOptions {
  std::filesystem::path folder;
  std::filesystem::path defaults;
  std::optional<std::string> output_prefix;
  std::string executable;
  bool powershell = false;
};

// Returns a complete script or an error; never emits a partial script.
absl::StatusOr<std::string> GenerateSessionCommands(
    const SessionCommandOptions& options);

}  // namespace racevideo

#endif  // RACEVIDEO_TOOLS_SESSION_COMMANDS_H_
