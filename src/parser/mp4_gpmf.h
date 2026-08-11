#ifndef RACEVIDEO_PARSER_MP4_GPMF_H_
#define RACEVIDEO_PARSER_MP4_GPMF_H_

#include <cstdint>
#include <filesystem>

#include "absl/status/statusor.h"

namespace racevideo {

struct GpmfTrackInfo {
  std::uint64_t file_size_bytes;
};

// Locates a GPMF metadata sample description in an ISO base media file.
// Payload indexing and decoding are separate, later stages.
absl::StatusOr<GpmfTrackInfo> FindGpmfTrack(
    const std::filesystem::path& input_path);

}  // namespace racevideo

#endif  // RACEVIDEO_PARSER_MP4_GPMF_H_

