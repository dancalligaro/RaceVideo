#ifndef RACEVIDEO_PARSER_TELEMETRY_DECODER_H_
#define RACEVIDEO_PARSER_TELEMETRY_DECODER_H_

#include <filesystem>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "parser/mp4_gpmf.h"
#include "telemetry/telemetry.h"

namespace racevideo {

absl::StatusOr<TelemetryData> DecodeTelemetry(
    const std::filesystem::path& input_path, const GpmfTrackInfo& track);

absl::Status WriteTelemetryJson(const TelemetryData& telemetry,
                                const std::filesystem::path& output_path);

}  // namespace racevideo

#endif  // RACEVIDEO_PARSER_TELEMETRY_DECODER_H_
