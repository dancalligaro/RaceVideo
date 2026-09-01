#ifndef RACEVIDEO_COMMON_INPUT_LIST_H_
#define RACEVIDEO_COMMON_INPUT_LIST_H_

#include <filesystem>
#include <vector>

#include "absl/status/statusor.h"

namespace racevideo {

absl::StatusOr<std::vector<std::filesystem::path>> ReadInputList(
    const std::filesystem::path& list_path);

}  // namespace racevideo

#endif  // RACEVIDEO_COMMON_INPUT_LIST_H_
