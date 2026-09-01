#include "common/input_list.h"

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace racevideo {

absl::StatusOr<std::vector<std::filesystem::path>> ReadInputList(
    const std::filesystem::path& list_path) {
  std::ifstream input(list_path);
  if (!input) {
    return absl::NotFoundError(
        absl::StrCat("cannot open input list: ", list_path.string()));
  }
  const std::filesystem::path base_directory = list_path.parent_path();
  std::vector<std::filesystem::path> paths;
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    std::string_view value = absl::StripAsciiWhitespace(line);
    if (value.empty()) continue;
    if (value.find('\0') != std::string_view::npos) {
      return absl::InvalidArgumentError(absl::StrCat(
          "input list contains a null byte on line ", line_number));
    }
    std::filesystem::path path{std::string(value)};
    if (path.is_relative()) path = base_directory / path;
    paths.push_back(path.lexically_normal());
  }
  if (!input.eof()) {
    return absl::DataLossError(
        absl::StrCat("cannot read input list: ", list_path.string()));
  }
  if (paths.empty()) {
    return absl::InvalidArgumentError("input list does not contain any files");
  }
  return paths;
}

}  // namespace racevideo
