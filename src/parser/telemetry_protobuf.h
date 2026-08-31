#ifndef RACEVIDEO_PARSER_TELEMETRY_PROTOBUF_H_
#define RACEVIDEO_PARSER_TELEMETRY_PROTOBUF_H_

#include <filesystem>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "telemetry/telemetry.h"

namespace racevideo {

absl::Status WriteTelemetryProtobuf(
    const TelemetryData& telemetry,
    const std::filesystem::path& output_path);

absl::StatusOr<TelemetryData> ReadTelemetryProtobuf(
    const std::filesystem::path& input_path);

}  // namespace racevideo

#endif  // RACEVIDEO_PARSER_TELEMETRY_PROTOBUF_H_
