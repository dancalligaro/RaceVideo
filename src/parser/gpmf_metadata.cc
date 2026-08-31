#include "parser/gpmf_metadata.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include "GPMF_parser.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace racevideo {
namespace {

constexpr std::uint32_t kMaximumPayloadSize = 64 * 1024 * 1024;

absl::Status Seek(std::istream& input, std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    return absl::OutOfRangeError("GPMF payload offset is too large");
  }
  input.clear();
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) return absl::DataLossError("cannot seek to GPMF payload");
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::uint32_t>> ReadPayload(
    std::istream& input, const GpmfPayload& payload) {
  if (payload.size_bytes == 0 || payload.size_bytes > kMaximumPayloadSize) {
    return absl::DataLossError("GPMF payload size is outside safe limits");
  }
  absl::Status status = Seek(input, payload.file_offset);
  if (!status.ok()) return status;
  std::vector<std::uint32_t> words((payload.size_bytes + 3) / 4, 0);
  if (!input.read(reinterpret_cast<char*>(words.data()), payload.size_bytes)) {
    return absl::DataLossError("truncated GPMF payload");
  }
  return words;
}

std::string FourCc(std::uint32_t key) {
  std::array<char, 4> chars = {
      static_cast<char>(key), static_cast<char>(key >> 8),
      static_cast<char>(key >> 16), static_cast<char>(key >> 24)};
  return std::string(chars.data(), chars.size());
}

}  // namespace

absl::StatusOr<GpmfSummary> InspectGpmf(
    const std::filesystem::path& input_path, const GpmfTrackInfo& track) {
  std::ifstream input(input_path, std::ios::binary);
  if (!input) return absl::PermissionDeniedError("cannot open input file");
  GpmfSummary summary;
  summary.payload_count = track.payloads.size();
  for (const GpmfPayload& payload : track.payloads) {
    absl::StatusOr<std::vector<std::uint32_t>> words =
        ReadPayload(input, payload);
    if (!words.ok()) return words.status();
    GPMF_stream stream{};
    if (GPMF_OK != GPMF_Init(&stream, words->data(), payload.size_bytes)) {
      return absl::DataLossError("GoPro parser rejected a GPMF payload");
    }
    do {
      summary.samples_by_key[FourCc(GPMF_Key(&stream))] +=
          GPMF_PayloadSampleCount(&stream);
    } while (GPMF_OK == GPMF_Next(&stream, GPMF_RECURSE_LEVELS));
    GPMF_Free(&stream);
    summary.total_bytes += payload.size_bytes;
  }
  return summary;
}

absl::Status ExtractRawGpmf(const std::filesystem::path& input_path,
                            const GpmfTrackInfo& track,
                            const std::filesystem::path& output_path) {
  std::error_code equivalent_error;
  if (std::filesystem::equivalent(input_path, output_path, equivalent_error) &&
      !equivalent_error) {
    return absl::InvalidArgumentError(
        "GPMF output path must not overwrite the input video");
  }
  std::ifstream input(input_path, std::ios::binary);
  if (!input) return absl::PermissionDeniedError("cannot open input file");
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return absl::PermissionDeniedError(
        absl::StrCat("cannot create output file: ", output_path.string()));
  }
  for (const GpmfPayload& payload : track.payloads) {
    absl::StatusOr<std::vector<std::uint32_t>> words =
        ReadPayload(input, payload);
    if (!words.ok()) return words.status();
    output.write(reinterpret_cast<const char*>(words->data()),
                 payload.size_bytes);
    if (!output) return absl::DataLossError("failed writing GPMF output");
  }
  return absl::OkStatus();
}

}  // namespace racevideo
