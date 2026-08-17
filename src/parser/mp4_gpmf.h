#ifndef RACEVIDEO_PARSER_MP4_GPMF_H_
#define RACEVIDEO_PARSER_MP4_GPMF_H_

#include <cstdint>
#include <filesystem>
#include <vector>

#include "absl/status/statusor.h"

namespace racevideo {

struct GpmfPayload {
  std::uint64_t file_offset;
  std::uint32_t size_bytes;
  std::uint64_t start_time_units;
  std::uint32_t duration_units;
};

struct GpmfTrackInfo {
  std::uint64_t file_size_bytes;
  std::uint32_t timescale = 0;
  std::vector<GpmfPayload> payloads;
};

// Locates a GPMF metadata sample description in an ISO base media file.
// Payload indexing and decoding are separate, later stages.
absl::StatusOr<GpmfTrackInfo> FindGpmfTrack(
    const std::filesystem::path& input_path);

absl::StatusOr<GpmfTrackInfo> IndexGpmfTrack(
    const std::filesystem::path& input_path);

}  // namespace racevideo

#endif  // RACEVIDEO_PARSER_MP4_GPMF_H_
