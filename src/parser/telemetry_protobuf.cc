#include "parser/telemetry_protobuf.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "proto/telemetry.pb.h"
#include "telemetry/mount_orientation.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

namespace racevideo {
namespace {

constexpr std::uint32_t kTelemetryFormatVersion = 1;
constexpr std::uintmax_t kMaximumTelemetryFileSize = 256 * 1024 * 1024;
constexpr int kMaximumSamplesPerStream = 10'000'000;

MountOrientation ParseMountOrientation(std::string_view name) {
  if (name == "upright") return MountOrientation::kUpright;
  if (name == "upside_down") return MountOrientation::kUpsideDown;
  if (name == "left_side_down") return MountOrientation::kLeftSideDown;
  if (name == "right_side_down") return MountOrientation::kRightSideDown;
  return MountOrientation::kUnknown;
}

std::int64_t TimestampMicros(absl::Duration timestamp) {
  return absl::ToInt64Microseconds(timestamp);
}

void AddVectorSamples(
    const std::vector<TimedSample<Vector3>>& source,
    google::protobuf::RepeatedPtrField<proto::Vector3Sample>* destination) {
  destination->Reserve(static_cast<int>(source.size()));
  for (const TimedSample<Vector3>& sample : source) {
    proto::Vector3Sample* encoded = destination->Add();
    encoded->set_timestamp_us(TimestampMicros(sample.timestamp));
    encoded->set_component_0(static_cast<float>(sample.value.x));
    encoded->set_component_1(static_cast<float>(sample.value.y));
    encoded->set_component_2(static_cast<float>(sample.value.z));
  }
}

absl::Status ValidateSampleCounts(const proto::TelemetryFile& file) {
  if (file.gps_size() > kMaximumSamplesPerStream ||
      file.accelerometer_size() > kMaximumSamplesPerStream ||
      file.gyroscope_size() > kMaximumSamplesPerStream ||
      file.filtered_g_force_size() > kMaximumSamplesPerStream) {
    return absl::ResourceExhaustedError(
        "telemetry stream exceeds the sample-count limit");
  }
  return absl::OkStatus();
}

void DecodeVectorSamples(
    const google::protobuf::RepeatedPtrField<proto::Vector3Sample>& source,
    std::vector<TimedSample<Vector3>>* destination) {
  destination->reserve(static_cast<std::size_t>(source.size()));
  for (const proto::Vector3Sample& sample : source) {
    destination->push_back(
        {.timestamp = absl::Microseconds(sample.timestamp_us()),
         .value = {.x = sample.component_0(),
                   .y = sample.component_1(),
                   .z = sample.component_2()}});
  }
}

}  // namespace

absl::Status WriteTelemetryProtobuf(
    const TelemetryData& telemetry,
    const std::filesystem::path& output_path) {
  proto::TelemetryFile file;
  file.set_format_version(kTelemetryFormatVersion);
  file.set_accelerometer_source_axis_order(
      telemetry.acceleration_metadata.source_axis_order);
  file.set_accelerometer_values_are_camera_xyz(
      telemetry.acceleration_metadata.values_are_camera_xyz);
  file.set_gyroscope_source_axis_order(
      telemetry.angular_velocity_metadata.source_axis_order);
  file.set_gyroscope_values_are_camera_xyz(
      telemetry.angular_velocity_metadata.values_are_camera_xyz);
  file.set_mount_orientation(std::string(MountOrientationName(
      telemetry.acceleration_metadata.mount_orientation)));
  file.set_mount_orientation_confidence(
      telemetry.acceleration_metadata.mount_orientation_confidence);
  file.set_inertial_values_are_forward_upright(
      telemetry.acceleration_metadata.values_are_forward_upright);
  file.mutable_gps()->Reserve(static_cast<int>(telemetry.gps.size()));
  for (const TimedSample<GpsReading>& sample : telemetry.gps) {
    proto::GpsSample* encoded = file.add_gps();
    encoded->set_timestamp_us(TimestampMicros(sample.timestamp));
    encoded->set_latitude_degrees(sample.value.latitude_degrees);
    encoded->set_longitude_degrees(sample.value.longitude_degrees);
    encoded->set_altitude_meters(sample.value.altitude_meters);
    encoded->set_ground_speed_meters_per_second(
        static_cast<float>(sample.value.ground_speed_meters_per_second));
    encoded->set_speed_3d_meters_per_second(
        static_cast<float>(sample.value.speed_3d_meters_per_second));
  }
  AddVectorSamples(telemetry.acceleration_meters_per_second_squared,
                   file.mutable_accelerometer());
  AddVectorSamples(telemetry.angular_velocity_radians_per_second,
                   file.mutable_gyroscope());
  file.set_g_force_filter_cutoff_hz(telemetry.g_force_filter_cutoff_hz);
  file.mutable_filtered_g_force()->Reserve(
      static_cast<int>(telemetry.filtered_g_force.size()));
  for (const TimedSample<GForceReading>& sample :
       telemetry.filtered_g_force) {
    proto::GForceSample* encoded = file.add_filtered_g_force();
    encoded->set_timestamp_us(TimestampMicros(sample.timestamp));
    encoded->set_lateral_g(static_cast<float>(sample.value.lateral_g));
    encoded->set_longitudinal_g(
        static_cast<float>(sample.value.longitudinal_g));
    encoded->set_vertical_dynamic_g(
        static_cast<float>(sample.value.vertical_dynamic_g));
  }

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return absl::PermissionDeniedError(
        absl::StrCat("cannot create output file: ", output_path.string()));
  }
  if (!file.SerializeToOstream(&output) || !output) {
    return absl::DataLossError("failed writing telemetry Protobuf");
  }
  return absl::OkStatus();
}

absl::StatusOr<TelemetryData> ReadTelemetryProtobuf(
    const std::filesystem::path& input_path) {
  std::error_code size_error;
  const std::uintmax_t file_size =
      std::filesystem::file_size(input_path, size_error);
  if (size_error) {
    return absl::NotFoundError(absl::StrCat(
        "cannot inspect telemetry file: ", input_path.string(), ": ",
        size_error.message()));
  }
  if (file_size == 0 || file_size > kMaximumTelemetryFileSize) {
    return absl::ResourceExhaustedError(
        "telemetry file size is outside safe limits");
  }

  std::ifstream input(input_path, std::ios::binary);
  if (!input) return absl::PermissionDeniedError("cannot open telemetry file");
  google::protobuf::io::IstreamInputStream raw_input(&input);
  google::protobuf::io::CodedInputStream coded_input(&raw_input);
  coded_input.SetTotalBytesLimit(
      static_cast<int>(kMaximumTelemetryFileSize));
  proto::TelemetryFile file;
  if (!file.ParseFromCodedStream(&coded_input) ||
      !coded_input.ConsumedEntireMessage()) {
    return absl::DataLossError("malformed or truncated telemetry Protobuf");
  }
  if (file.format_version() != kTelemetryFormatVersion) {
    return absl::FailedPreconditionError(absl::StrCat(
        "unsupported telemetry format version: ", file.format_version()));
  }
  absl::Status status = ValidateSampleCounts(file);
  if (!status.ok()) return status;

  TelemetryData telemetry;
  telemetry.acceleration_metadata = {
      .source_axis_order = file.accelerometer_source_axis_order(),
      .values_are_camera_xyz =
          file.accelerometer_values_are_camera_xyz(),
      .mount_orientation = ParseMountOrientation(file.mount_orientation()),
      .mount_orientation_confidence = file.mount_orientation_confidence(),
      .values_are_forward_upright =
          file.inertial_values_are_forward_upright()};
  telemetry.angular_velocity_metadata = {
      .source_axis_order = file.gyroscope_source_axis_order(),
      .values_are_camera_xyz = file.gyroscope_values_are_camera_xyz(),
      .mount_orientation = ParseMountOrientation(file.mount_orientation()),
      .mount_orientation_confidence = file.mount_orientation_confidence(),
      .values_are_forward_upright =
          file.inertial_values_are_forward_upright()};
  telemetry.gps.reserve(static_cast<std::size_t>(file.gps_size()));
  for (const proto::GpsSample& sample : file.gps()) {
    telemetry.gps.push_back(
        {.timestamp = absl::Microseconds(sample.timestamp_us()),
         .value = {
             .latitude_degrees = sample.latitude_degrees(),
             .longitude_degrees = sample.longitude_degrees(),
             .altitude_meters = sample.altitude_meters(),
             .ground_speed_meters_per_second =
                 sample.ground_speed_meters_per_second(),
             .speed_3d_meters_per_second =
                 sample.speed_3d_meters_per_second()}});
  }
  DecodeVectorSamples(file.accelerometer(),
                      &telemetry.acceleration_meters_per_second_squared);
  DecodeVectorSamples(file.gyroscope(),
                      &telemetry.angular_velocity_radians_per_second);
  telemetry.g_force_filter_cutoff_hz = file.g_force_filter_cutoff_hz();
  telemetry.filtered_g_force.reserve(
      static_cast<std::size_t>(file.filtered_g_force_size()));
  for (const proto::GForceSample& sample : file.filtered_g_force()) {
    telemetry.filtered_g_force.push_back(
        {.timestamp = absl::Microseconds(sample.timestamp_us()),
         .value = {.lateral_g = sample.lateral_g(),
                   .longitudinal_g = sample.longitudinal_g(),
                   .vertical_dynamic_g = sample.vertical_dynamic_g()}});
  }
  return telemetry;
}

}  // namespace racevideo
