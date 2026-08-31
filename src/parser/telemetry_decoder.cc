#include "parser/telemetry_decoder.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <vector>

#include "GPMF_parser.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "telemetry/mount_orientation.h"

namespace racevideo {
namespace {

constexpr std::uint32_t kGps5Key =
    static_cast<std::uint32_t>('G') |
    (static_cast<std::uint32_t>('P') << 8) |
    (static_cast<std::uint32_t>('S') << 16) |
    (static_cast<std::uint32_t>('5') << 24);
constexpr std::uint32_t kAcclKey =
    static_cast<std::uint32_t>('A') |
    (static_cast<std::uint32_t>('C') << 8) |
    (static_cast<std::uint32_t>('C') << 16) |
    (static_cast<std::uint32_t>('L') << 24);
constexpr std::uint32_t kGyroKey =
    static_cast<std::uint32_t>('G') |
    (static_cast<std::uint32_t>('Y') << 8) |
    (static_cast<std::uint32_t>('R') << 16) |
    (static_cast<std::uint32_t>('O') << 24);
constexpr std::uint32_t kMaximumPayloadSize = 64 * 1024 * 1024;
constexpr std::uint32_t kMaximumSamplesPerPayload = 1'000'000;

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

absl::Status AppendGps5(GPMF_stream* stream, const GpmfPayload& payload,
                        std::uint32_t timescale, TelemetryData* telemetry) {
  const std::uint32_t samples = GPMF_Repeat(stream);
  const std::uint32_t elements = GPMF_ElementsInStruct(stream);
  if (samples == 0) return absl::OkStatus();
  if (samples > kMaximumSamplesPerPayload || elements != 5) {
    return absl::DataLossError("GPS5 dimensions are outside safe limits");
  }
  std::vector<double> values(static_cast<std::size_t>(samples) * elements);
  const std::uint32_t bytes = static_cast<std::uint32_t>(
      values.size() * sizeof(values.front()));
  if (GPMF_OK != GPMF_ScaledData(stream, values.data(), bytes, 0, samples,
                                 GPMF_TYPE_DOUBLE)) {
    return absl::DataLossError("cannot scale GPS5 telemetry");
  }

  const double payload_start =
      static_cast<double>(payload.start_time_units) / timescale;
  const double sample_period =
      static_cast<double>(payload.duration_units) / timescale / samples;
  telemetry->gps.reserve(telemetry->gps.size() + samples);
  for (std::uint32_t i = 0; i < samples; ++i) {
    const std::size_t position = static_cast<std::size_t>(i) * elements;
    telemetry->gps.push_back({
        .timestamp = absl::Seconds(payload_start + i * sample_period),
        .value = {.latitude_degrees = values[position],
                  .longitude_degrees = values[position + 1],
                  .altitude_meters = values[position + 2],
                  .ground_speed_meters_per_second = values[position + 3],
                  .speed_3d_meters_per_second = values[position + 4]}});
  }
  return absl::OkStatus();
}

absl::Status AppendVector3(GPMF_stream* stream, const GpmfPayload& payload,
                           std::uint32_t timescale,
                           std::vector<TimedSample<Vector3>>* destination) {
  const std::uint32_t samples = GPMF_Repeat(stream);
  const std::uint32_t elements = GPMF_ElementsInStruct(stream);
  if (samples == 0) return absl::OkStatus();
  if (samples > kMaximumSamplesPerPayload || elements != 3) {
    return absl::DataLossError(
        "inertial sensor dimensions are outside safe limits");
  }
  std::vector<double> values(static_cast<std::size_t>(samples) * elements);
  const std::uint32_t bytes = static_cast<std::uint32_t>(
      values.size() * sizeof(values.front()));
  if (GPMF_OK != GPMF_ScaledData(stream, values.data(), bytes, 0, samples,
                                 GPMF_TYPE_DOUBLE)) {
    return absl::DataLossError("cannot scale inertial telemetry");
  }

  const double payload_start =
      static_cast<double>(payload.start_time_units) / timescale;
  const double sample_period =
      static_cast<double>(payload.duration_units) / timescale / samples;
  destination->reserve(destination->size() + samples);
  for (std::uint32_t i = 0; i < samples; ++i) {
    const std::size_t position = static_cast<std::size_t>(i) * elements;
    destination->push_back(
        {.timestamp = absl::Seconds(payload_start + i * sample_period),
         .value = {.x = values[position],
                   .y = values[position + 1],
                   .z = values[position + 2]}});
  }
  return absl::OkStatus();
}

void WriteVectorSamples(std::ostream& output,
                        const std::vector<TimedSample<Vector3>>& samples) {
  output << '[';
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto& sample = samples[i];
    output << (i == 0 ? "\n" : ",\n")
           << "    {\"timestamp_seconds\": "
           << absl::ToDoubleSeconds(sample.timestamp)
           << ", \"component_0\": " << sample.value.x
           << ", \"component_1\": " << sample.value.y
           << ", \"component_2\": " << sample.value.z << '}';
  }
  output << (samples.empty() ? "" : "\n") << "  ]";
}

void WriteGForceSamples(
    std::ostream& output,
    const std::vector<TimedSample<GForceReading>>& samples) {
  output << '[';
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto& sample = samples[i];
    output << (i == 0 ? "\n" : ",\n")
           << "    {\"timestamp_seconds\": "
           << absl::ToDoubleSeconds(sample.timestamp)
           << ", \"lateral_g\": " << sample.value.lateral_g
           << ", \"longitudinal_g\": "
           << sample.value.longitudinal_g
           << ", \"vertical_dynamic_g\": "
           << sample.value.vertical_dynamic_g << '}';
  }
  output << (samples.empty() ? "" : "\n") << "  ]";
}

}  // namespace

absl::StatusOr<TelemetryData> DecodeTelemetry(
    const std::filesystem::path& input_path, const GpmfTrackInfo& track) {
  if (track.timescale == 0) {
    return absl::DataLossError("GPMF track has a zero timescale");
  }
  std::ifstream input(input_path, std::ios::binary);
  if (!input) return absl::PermissionDeniedError("cannot open input file");
  TelemetryData telemetry;
  for (const GpmfPayload& payload : track.payloads) {
    absl::StatusOr<std::vector<std::uint32_t>> words =
        ReadPayload(input, payload);
    if (!words.ok()) return words.status();
    GPMF_stream stream{};
    if (GPMF_OK != GPMF_Init(&stream, words->data(), payload.size_bytes)) {
      return absl::DataLossError("GoPro parser rejected a GPMF payload");
    }
    while (GPMF_OK == GPMF_FindNext(
                          &stream, kGps5Key,
                          static_cast<GPMF_LEVELS>(GPMF_RECURSE_LEVELS |
                                                   GPMF_TOLERANT))) {
      const absl::Status status =
          AppendGps5(&stream, payload, track.timescale, &telemetry);
      if (!status.ok()) {
        GPMF_Free(&stream);
        return status;
      }
    }
    GPMF_ResetState(&stream);
    while (GPMF_OK == GPMF_FindNext(
                          &stream, kAcclKey,
                          static_cast<GPMF_LEVELS>(GPMF_RECURSE_LEVELS |
                                                   GPMF_TOLERANT))) {
      const absl::Status status = AppendVector3(
          &stream, payload, track.timescale,
          &telemetry.acceleration_meters_per_second_squared);
      if (!status.ok()) {
        GPMF_Free(&stream);
        return status;
      }
    }
    GPMF_ResetState(&stream);
    while (GPMF_OK == GPMF_FindNext(
                          &stream, kGyroKey,
                          static_cast<GPMF_LEVELS>(GPMF_RECURSE_LEVELS |
                                                   GPMF_TOLERANT))) {
      const absl::Status status = AppendVector3(
          &stream, payload, track.timescale,
          &telemetry.angular_velocity_radians_per_second);
      if (!status.ok()) {
        GPMF_Free(&stream);
        return status;
      }
    }
    GPMF_Free(&stream);
  }
  return telemetry;
}

absl::Status WriteTelemetryJson(const TelemetryData& telemetry,
                                const std::filesystem::path& output_path) {
  std::ofstream output(output_path, std::ios::trunc);
  if (!output) {
    return absl::PermissionDeniedError(
        absl::StrCat("cannot create output file: ", output_path.string()));
  }
  output << std::setprecision(15)
         << "{\n  \"inertial_metadata\": {"
         << "\n    \"source_axis_order\": \""
         << telemetry.acceleration_metadata.source_axis_order << "\","
         << "\n    \"values_are_camera_xyz\": "
         << (telemetry.acceleration_metadata.values_are_camera_xyz ? "true"
                                                                   : "false")
         << ",\n    \"mount_orientation\": \""
         << MountOrientationName(
                telemetry.acceleration_metadata.mount_orientation)
         << "\",\n    \"mount_orientation_confidence\": "
         << telemetry.acceleration_metadata.mount_orientation_confidence
         << ",\n    \"values_are_forward_upright\": "
         << (telemetry.acceleration_metadata.values_are_forward_upright
                 ? "true"
                 : "false")
         << "\n  },\n  \"gps\": [";
  for (std::size_t i = 0; i < telemetry.gps.size(); ++i) {
    const auto& sample = telemetry.gps[i];
    const auto& gps = sample.value;
    output << (i == 0 ? "\n" : ",\n")
           << "    {\"timestamp_seconds\": "
           << absl::ToDoubleSeconds(sample.timestamp)
           << ", \"latitude_degrees\": " << gps.latitude_degrees
           << ", \"longitude_degrees\": " << gps.longitude_degrees
           << ", \"altitude_meters\": " << gps.altitude_meters
           << ", \"ground_speed_meters_per_second\": "
           << gps.ground_speed_meters_per_second
           << ", \"speed_3d_meters_per_second\": "
           << gps.speed_3d_meters_per_second << '}';
  }
  output << (telemetry.gps.empty() ? "" : "\n")
         << "  ],\n  \"accelerometer_meters_per_second_squared\": ";
  WriteVectorSamples(
      output, telemetry.acceleration_meters_per_second_squared);
  output << ",\n  \"gyroscope_radians_per_second\": ";
  WriteVectorSamples(output, telemetry.angular_velocity_radians_per_second);
  output << ",\n  \"g_force_filter_cutoff_hz\": "
         << telemetry.g_force_filter_cutoff_hz
         << ",\n  \"filtered_g_force\": ";
  WriteGForceSamples(output, telemetry.filtered_g_force);
  output << "\n}\n";
  if (!output) return absl::DataLossError("failed writing telemetry JSON");
  return absl::OkStatus();
}

}  // namespace racevideo
