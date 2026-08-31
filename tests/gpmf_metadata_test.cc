#include "parser/gpmf_metadata.h"
#include "parser/telemetry_decoder.h"
#include "parser/telemetry_protobuf.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "proto/telemetry.pb.h"

namespace racevideo {
namespace {

TEST(ExtractRawGpmfTest, WritesPayloadBytesInTrackOrder) {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path();
  const std::filesystem::path input_path = directory / "racevideo_raw_input.bin";
  const std::filesystem::path output_path =
      directory / "racevideo_raw_output.gpmf";
  {
    std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
    input << "abcdefgh";
  }
  GpmfTrackInfo track;
  track.payloads = {{.file_offset = 1, .size_bytes = 3},
                    {.file_offset = 5, .size_bytes = 3}};

  const absl::Status status =
      ExtractRawGpmf(input_path, track, output_path);

  ASSERT_TRUE(status.ok()) << status;
  std::ifstream output(output_path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(output)),
                          std::istreambuf_iterator<char>());
  EXPECT_EQ(bytes, "bcdfgh");
  std::error_code ignored;
  std::filesystem::remove(input_path, ignored);
  std::filesystem::remove(output_path, ignored);
}

TEST(ExtractRawGpmfTest, RefusesToOverwriteInputVideo) {
  const std::filesystem::path input_path =
      std::filesystem::temp_directory_path() / "racevideo_same_path.bin";
  {
    std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
    input << "metadata";
  }
  GpmfTrackInfo track;
  track.payloads = {{.file_offset = 0, .size_bytes = 8}};

  const absl::Status status =
      ExtractRawGpmf(input_path, track, input_path);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(std::filesystem::file_size(input_path), 8);
  std::error_code ignored;
  std::filesystem::remove(input_path, ignored);
}

TEST(WriteTelemetryJsonTest, WritesGpsValuesAndTimestamps) {
  const std::filesystem::path output_path =
      std::filesystem::temp_directory_path() / "racevideo_telemetry.json";
  TelemetryData telemetry;
  telemetry.gps.push_back(
      {.timestamp = absl::Seconds(1.25),
       .value = {.latitude_degrees = 41.5,
                 .longitude_degrees = -88.25,
                 .altitude_meters = 210.0,
                 .ground_speed_meters_per_second = 12.5,
                 .speed_3d_meters_per_second = 12.75}});
  telemetry.acceleration_meters_per_second_squared.push_back(
      {.timestamp = absl::Seconds(0.5), .value = {.x = 1, .y = 2, .z = 3}});
  telemetry.angular_velocity_radians_per_second.push_back(
      {.timestamp = absl::Seconds(0.25),
       .value = {.x = 0.1, .y = 0.2, .z = 0.3}});

  const absl::Status status = WriteTelemetryJson(telemetry, output_path);

  ASSERT_TRUE(status.ok()) << status;
  std::ifstream output(output_path);
  const std::string json((std::istreambuf_iterator<char>(output)),
                         std::istreambuf_iterator<char>());
  EXPECT_NE(json.find("\"timestamp_seconds\": 1.25"), std::string::npos);
  EXPECT_NE(json.find("\"latitude_degrees\": 41.5"), std::string::npos);
  EXPECT_NE(json.find("\"longitude_degrees\": -88.25"),
            std::string::npos);
  EXPECT_NE(json.find("\"accelerometer_meters_per_second_squared\""),
            std::string::npos);
  EXPECT_NE(json.find("\"component_2\": 3"), std::string::npos);
  EXPECT_NE(json.find("\"gyroscope_radians_per_second\""),
            std::string::npos);
  std::error_code ignored;
  std::filesystem::remove(output_path, ignored);
}

TEST(WriteTelemetryProtobufTest, PreservesStreamsAndUsesCompactTypes) {
  const std::filesystem::path output_path =
      std::filesystem::temp_directory_path() / "racevideo_telemetry.rvt";
  TelemetryData telemetry;
  telemetry.gps.push_back(
      {.timestamp = absl::Microseconds(1'250'000),
       .value = {.latitude_degrees = 41.5,
                 .longitude_degrees = -88.25,
                 .altitude_meters = 210.0,
                 .ground_speed_meters_per_second = 12.5,
                 .speed_3d_meters_per_second = 12.75}});
  telemetry.acceleration_meters_per_second_squared.push_back(
      {.timestamp = absl::Microseconds(500'000),
       .value = {.x = 1, .y = 2, .z = 3}});
  telemetry.acceleration_metadata = {
      .source_axis_order = "YxZ", .values_are_camera_xyz = true};
  telemetry.filtered_g_force.push_back(
      {.timestamp = absl::Microseconds(500'000),
       .value = {.lateral_g = 0.1,
                 .longitudinal_g = -0.2,
                 .vertical_dynamic_g = 0.3}});
  telemetry.g_force_filter_cutoff_hz = 5.0;

  const absl::Status status =
      WriteTelemetryProtobuf(telemetry, output_path);

  ASSERT_TRUE(status.ok()) << status;
  std::ifstream input(output_path, std::ios::binary);
  proto::TelemetryFile decoded;
  ASSERT_TRUE(decoded.ParseFromIstream(&input));
  EXPECT_EQ(decoded.format_version(), 1);
  ASSERT_EQ(decoded.gps_size(), 1);
  EXPECT_EQ(decoded.gps(0).timestamp_us(), 1'250'000);
  EXPECT_DOUBLE_EQ(decoded.gps(0).latitude_degrees(), 41.5);
  ASSERT_EQ(decoded.accelerometer_size(), 1);
  EXPECT_FLOAT_EQ(decoded.accelerometer(0).component_2(), 3.0F);
  EXPECT_EQ(decoded.gyroscope_size(), 0);
  const absl::StatusOr<TelemetryData> round_trip =
      ReadTelemetryProtobuf(output_path);
  ASSERT_TRUE(round_trip.ok()) << round_trip.status();
  ASSERT_EQ(round_trip->gps.size(), 1);
  EXPECT_EQ(round_trip->gps[0].timestamp, absl::Microseconds(1'250'000));
  EXPECT_DOUBLE_EQ(round_trip->gps[0].value.longitude_degrees, -88.25);
  ASSERT_EQ(round_trip->acceleration_meters_per_second_squared.size(), 1);
  EXPECT_DOUBLE_EQ(
      round_trip->acceleration_meters_per_second_squared[0].value.z, 3.0);
  EXPECT_EQ(round_trip->acceleration_metadata.source_axis_order, "YxZ");
  EXPECT_TRUE(round_trip->acceleration_metadata.values_are_camera_xyz);
  ASSERT_EQ(round_trip->filtered_g_force.size(), 1);
  EXPECT_NEAR(round_trip->filtered_g_force[0].value.longitudinal_g, -0.2,
              1e-6);
  EXPECT_DOUBLE_EQ(round_trip->g_force_filter_cutoff_hz, 5.0);
  std::error_code ignored;
  std::filesystem::remove(output_path, ignored);
}

TEST(ReadTelemetryProtobufTest, RejectsMalformedInput) {
  const std::filesystem::path input_path =
      std::filesystem::temp_directory_path() / "racevideo_malformed.rvt";
  {
    std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
    input << "not a protobuf";
  }

  const absl::StatusOr<TelemetryData> telemetry =
      ReadTelemetryProtobuf(input_path);

  EXPECT_EQ(telemetry.status().code(), absl::StatusCode::kDataLoss);
  std::error_code ignored;
  std::filesystem::remove(input_path, ignored);
}

TEST(ReadTelemetryProtobufTest, RejectsUnsupportedFormatVersion) {
  const std::filesystem::path input_path =
      std::filesystem::temp_directory_path() / "racevideo_future.rvt";
  proto::TelemetryFile file;
  file.set_format_version(999);
  {
    std::ofstream output(input_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.SerializeToOstream(&output));
  }

  const absl::StatusOr<TelemetryData> telemetry =
      ReadTelemetryProtobuf(input_path);

  EXPECT_EQ(telemetry.status().code(),
            absl::StatusCode::kFailedPrecondition);
  std::error_code ignored;
  std::filesystem::remove(input_path, ignored);
}

}  // namespace
}  // namespace racevideo
