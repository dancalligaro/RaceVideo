#include "common/run.h"

#include <filesystem>
#include <system_error>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "parser/mp4_gpmf.h"

namespace racevideo {

absl::Status Run(const Options& options) {
  std::error_code error;
  const bool exists = std::filesystem::exists(options.input_path, error);
  if (error) {
    return absl::UnknownError(absl::StrCat(
        "cannot inspect input path: ", options.input_path.string(), ": ",
        error.message()));
  }
  if (!exists) {
    return absl::NotFoundError(
        absl::StrCat("input file not found: ", options.input_path.string()));
  }

  const bool is_regular_file =
      std::filesystem::is_regular_file(options.input_path, error);
  if (error) {
    return absl::UnknownError(absl::StrCat(
        "cannot inspect input path: ", options.input_path.string(), ": ",
        error.message()));
  }
  if (!is_regular_file) {
    return absl::InvalidArgumentError(
        absl::StrCat("input is not a file: ", options.input_path.string()));
  }

  absl::StatusOr<GpmfTrackInfo> track = FindGpmfTrack(options.input_path);
  if (!track.ok()) return track.status();

  return absl::UnimplementedError(absl::StrCat(
      "found GPMF metadata in ", track->file_size_bytes,
      "-byte file; payload indexing is the next implementation milestone"));
}

}  // namespace racevideo
