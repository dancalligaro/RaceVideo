#ifndef RACEVIDEO_PARSER_GPMF_METADATA_H_
#define RACEVIDEO_PARSER_GPMF_METADATA_H_

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "parser/mp4_gpmf.h"

namespace racevideo {

struct GpmfSummary {
  std::uint64_t payload_count = 0;
  std::uint64_t total_bytes = 0;
  std::map<std::string, std::uint64_t> samples_by_key;
};

absl::StatusOr<GpmfSummary> InspectGpmf(
    const std::filesystem::path& input_path, const GpmfTrackInfo& track);

absl::Status ExtractRawGpmf(const std::filesystem::path& input_path,
                            const GpmfTrackInfo& track,
                            const std::filesystem::path& output_path);

}  // namespace racevideo

#endif  // RACEVIDEO_PARSER_GPMF_METADATA_H_
