#include "telemetry/axis_mapping.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace racevideo {
namespace {

struct ComponentMapping {
  std::size_t output_index;
  double sign;
};

absl::StatusOr<std::array<ComponentMapping, 3>> ParseOrder(
    std::string_view order) {
  if (order.size() != 3) {
    return absl::InvalidArgumentError(
        "IMU axis order must contain exactly three characters");
  }
  std::array<ComponentMapping, 3> mapping{};
  std::array<bool, 3> seen{};
  for (std::size_t input_index = 0; input_index < order.size(); ++input_index) {
    const unsigned char character =
        static_cast<unsigned char>(order[input_index]);
    const char axis = static_cast<char>(std::toupper(character));
    if (axis < 'X' || axis > 'Z') {
      return absl::InvalidArgumentError(absl::StrCat(
          "invalid IMU axis order: ", std::string(order)));
    }
    const std::size_t output_index = static_cast<std::size_t>(axis - 'X');
    if (seen[output_index]) {
      return absl::InvalidArgumentError(
          "IMU axis order must contain X, Y, and Z exactly once");
    }
    seen[output_index] = true;
    mapping[input_index] = {
        .output_index = output_index,
        .sign = std::islower(character) != 0 ? -1.0 : 1.0};
  }
  return mapping;
}

void ApplyMapping(const std::array<ComponentMapping, 3>& mapping,
                  std::vector<TimedSample<Vector3>>* samples) {
  for (TimedSample<Vector3>& sample : *samples) {
    const std::array<double, 3> input = {
        sample.value.x, sample.value.y, sample.value.z};
    std::array<double, 3> output{};
    for (std::size_t i = 0; i < mapping.size(); ++i) {
      output[mapping[i].output_index] = input[i] * mapping[i].sign;
    }
    sample.value = {.x = output[0], .y = output[1], .z = output[2]};
  }
}

}  // namespace

absl::Status NormalizeInertialAxes(std::string_view source_axis_order,
                                   TelemetryData* telemetry) {
  absl::StatusOr<std::array<ComponentMapping, 3>> mapping =
      ParseOrder(source_axis_order);
  if (!mapping.ok()) return mapping.status();
  ApplyMapping(*mapping,
               &telemetry->acceleration_meters_per_second_squared);
  ApplyMapping(*mapping, &telemetry->angular_velocity_radians_per_second);
  telemetry->acceleration_metadata = {
      .source_axis_order = std::string(source_axis_order),
      .values_are_camera_xyz = true};
  telemetry->angular_velocity_metadata = telemetry->acceleration_metadata;
  return absl::OkStatus();
}

}  // namespace racevideo
