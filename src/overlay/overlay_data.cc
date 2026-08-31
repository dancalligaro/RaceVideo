#include "overlay/overlay_data.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace racevideo {
namespace {

constexpr double kEarthRadiusMeters = 6'371'008.8;
constexpr double kDegreesToRadians = 0.01745329251994329577;
constexpr double kRadiansToDegrees = 57.295779513082320876;
constexpr double kTrackPadding = 0.05;
constexpr double kMinimumHeadingBaselineMeters = 5.0;
constexpr double kMaximumPlausibleSegmentSpeedMetersPerSecond = 150.0;

struct ProjectedPoint {
  absl::Duration timestamp;
  double x_meters;
  double y_meters;
  double speed_meters_per_second;
};

bool IsValid(const TimedSample<GpsReading>& sample) {
  const GpsReading& reading = sample.value;
  return std::isfinite(reading.latitude_degrees) &&
         std::isfinite(reading.longitude_degrees) &&
         std::isfinite(reading.ground_speed_meters_per_second) &&
         reading.latitude_degrees >= -90.0 &&
         reading.latitude_degrees <= 90.0 &&
         reading.longitude_degrees >= -180.0 &&
         reading.longitude_degrees <= 180.0 &&
         reading.ground_speed_meters_per_second >= 0.0;
}

double GeographicDistance(const GpsReading& first, const GpsReading& second) {
  const double latitude_1 = first.latitude_degrees * kDegreesToRadians;
  const double latitude_2 = second.latitude_degrees * kDegreesToRadians;
  const double delta_latitude = latitude_2 - latitude_1;
  const double delta_longitude =
      (second.longitude_degrees - first.longitude_degrees) *
      kDegreesToRadians;
  const double sin_latitude = std::sin(delta_latitude / 2.0);
  const double sin_longitude = std::sin(delta_longitude / 2.0);
  const double haversine =
      sin_latitude * sin_latitude + std::cos(latitude_1) *
                                            std::cos(latitude_2) *
                                            sin_longitude * sin_longitude;
  return 2.0 * kEarthRadiusMeters *
         std::asin(std::sqrt(std::clamp(haversine, 0.0, 1.0)));
}

double Distance(const ProjectedPoint& first, const ProjectedPoint& second) {
  return std::hypot(second.x_meters - first.x_meters,
                    second.y_meters - first.y_meters);
}

double HeadingDegrees(const ProjectedPoint& first,
                      const ProjectedPoint& second) {
  double heading = std::atan2(second.x_meters - first.x_meters,
                              second.y_meters - first.y_meters) *
                   kRadiansToDegrees;
  if (heading < 0.0) heading += 360.0;
  return heading;
}

double Interpolate(double first, double second, double fraction) {
  return first + (second - first) * fraction;
}

double InterpolateHeading(double first, double second, double fraction) {
  double difference = std::fmod(second - first + 540.0, 360.0) - 180.0;
  double result = first + difference * fraction;
  result = std::fmod(result, 360.0);
  if (result < 0.0) result += 360.0;
  return result;
}

template <typename Sample>
std::size_t UpperSampleIndex(const std::vector<Sample>& samples,
                             absl::Duration timestamp) {
  return static_cast<std::size_t>(std::upper_bound(
      samples.begin(), samples.end(), timestamp,
      [](absl::Duration time, const Sample& sample) {
        return time < sample.timestamp;
      }) - samples.begin());
}

}  // namespace

absl::StatusOr<OverlayData> BuildOverlayData(const TelemetryData& telemetry) {
  std::vector<const TimedSample<GpsReading>*> valid;
  valid.reserve(telemetry.gps.size());
  for (const TimedSample<GpsReading>& sample : telemetry.gps) {
    if (!IsValid(sample)) continue;
    if (!valid.empty()) {
      const double delta_seconds =
          absl::ToDoubleSeconds(sample.timestamp - valid.back()->timestamp);
      if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) {
        return absl::DataLossError(
            "GPS timestamps must be strictly increasing");
      }
      const double segment_speed =
          GeographicDistance(valid.back()->value, sample.value) /
          delta_seconds;
      if (!std::isfinite(segment_speed) ||
          segment_speed > kMaximumPlausibleSegmentSpeedMetersPerSecond) {
        continue;
      }
    }
    valid.push_back(&sample);
  }
  if (valid.size() < 2) {
    return absl::FailedPreconditionError(
        "at least two valid GPS samples are required for an overlay");
  }

  double latitude_sum = 0.0;
  for (const auto* sample : valid) {
    latitude_sum += sample->value.latitude_degrees;
  }
  const double reference_latitude =
      latitude_sum / static_cast<double>(valid.size());
  const double cosine_latitude =
      std::cos(reference_latitude * kDegreesToRadians);
  const double reference_longitude = valid.front()->value.longitude_degrees;
  std::vector<ProjectedPoint> projected;
  projected.reserve(valid.size());
  for (const auto* sample : valid) {
    double longitude_delta =
        sample->value.longitude_degrees - reference_longitude;
    if (longitude_delta > 180.0) longitude_delta -= 360.0;
    if (longitude_delta < -180.0) longitude_delta += 360.0;
    projected.push_back({
        .timestamp = sample->timestamp,
        .x_meters = kEarthRadiusMeters * longitude_delta *
                    kDegreesToRadians * cosine_latitude,
        .y_meters = kEarthRadiusMeters *
                    (sample->value.latitude_degrees - reference_latitude) *
                    kDegreesToRadians,
        .speed_meters_per_second =
            sample->value.ground_speed_meters_per_second});
  }

  double minimum_x = projected.front().x_meters;
  double maximum_x = minimum_x;
  double minimum_y = projected.front().y_meters;
  double maximum_y = minimum_y;
  for (const ProjectedPoint& point : projected) {
    minimum_x = std::min(minimum_x, point.x_meters);
    maximum_x = std::max(maximum_x, point.x_meters);
    minimum_y = std::min(minimum_y, point.y_meters);
    maximum_y = std::max(maximum_y, point.y_meters);
  }
  const double width = maximum_x - minimum_x;
  const double height = maximum_y - minimum_y;
  const double extent = std::max(width, height);
  if (!std::isfinite(extent) || extent <= 0.0) {
    return absl::FailedPreconditionError("GPS path has no drawable extent");
  }
  const double scale = (1.0 - 2.0 * kTrackPadding) / extent;
  const double center_x = (minimum_x + maximum_x) / 2.0;
  const double center_y = (minimum_y + maximum_y) / 2.0;

  OverlayData overlay;
  overlay.track.reserve(projected.size());
  overlay.navigation.reserve(projected.size());
  std::size_t heading_target = 1;
  double last_heading = 0.0;
  for (std::size_t i = 0; i < projected.size(); ++i) {
    overlay.track.push_back(
        {.timestamp = projected[i].timestamp,
         .x = 0.5 + (projected[i].x_meters - center_x) * scale,
         .y = 0.5 - (projected[i].y_meters - center_y) * scale});
    heading_target = std::max(heading_target, i + 1);
    while (heading_target < projected.size() &&
           Distance(projected[i], projected[heading_target]) <
               kMinimumHeadingBaselineMeters) {
      ++heading_target;
    }
    if (heading_target < projected.size()) {
      last_heading = HeadingDegrees(projected[i], projected[heading_target]);
    }
    overlay.navigation.push_back(
        {.timestamp = projected[i].timestamp,
         .speed_meters_per_second = projected[i].speed_meters_per_second,
         .heading_degrees = last_heading});
  }
  return overlay;
}

absl::StatusOr<OverlayFrameData> SampleOverlayFrame(
    const TelemetryData& telemetry, const OverlayData& overlay,
    absl::Duration timestamp) {
  if (overlay.navigation.empty() || overlay.track.empty()) {
    return absl::FailedPreconditionError("overlay data is empty");
  }
  if (telemetry.filtered_g_force.empty()) {
    return absl::FailedPreconditionError(
        "filtered G-force is required for an overlay");
  }

  const std::size_t navigation_upper =
      UpperSampleIndex(overlay.navigation, timestamp);
  NavigationSample navigation = navigation_upper == 0
                                    ? overlay.navigation.front()
                                    : overlay.navigation[std::min(
                                          navigation_upper,
                                          overlay.navigation.size() - 1)];
  if (navigation_upper > 0 && navigation_upper < overlay.navigation.size()) {
    const NavigationSample& first = overlay.navigation[navigation_upper - 1];
    const NavigationSample& second = overlay.navigation[navigation_upper];
    const double interval =
        absl::ToDoubleSeconds(second.timestamp - first.timestamp);
    const double fraction =
        interval > 0.0
            ? absl::ToDoubleSeconds(timestamp - first.timestamp) / interval
            : 0.0;
    navigation.speed_meters_per_second =
        Interpolate(first.speed_meters_per_second,
                    second.speed_meters_per_second, fraction);
    navigation.heading_degrees =
        InterpolateHeading(first.heading_degrees, second.heading_degrees,
                           fraction);
  }

  const std::size_t g_force_upper =
      UpperSampleIndex(telemetry.filtered_g_force, timestamp);
  GForceReading g_force =
      g_force_upper == 0
          ? telemetry.filtered_g_force.front().value
          : telemetry.filtered_g_force[std::min(
                g_force_upper, telemetry.filtered_g_force.size() - 1)]
                .value;
  if (g_force_upper > 0 &&
      g_force_upper < telemetry.filtered_g_force.size()) {
    const auto& first = telemetry.filtered_g_force[g_force_upper - 1];
    const auto& second = telemetry.filtered_g_force[g_force_upper];
    const double interval =
        absl::ToDoubleSeconds(second.timestamp - first.timestamp);
    const double fraction =
        interval > 0.0
            ? absl::ToDoubleSeconds(timestamp - first.timestamp) / interval
            : 0.0;
    g_force = {
        .lateral_g =
            Interpolate(first.value.lateral_g, second.value.lateral_g,
                        fraction),
        .longitudinal_g = Interpolate(first.value.longitudinal_g,
                                      second.value.longitudinal_g, fraction),
        .vertical_dynamic_g = Interpolate(first.value.vertical_dynamic_g,
                                          second.value.vertical_dynamic_g,
                                          fraction)};
  }

  return OverlayFrameData{
      .speed_meters_per_second = navigation.speed_meters_per_second,
      .heading_degrees = navigation.heading_degrees,
      .g_force = g_force,
      .explored_track_point_count =
          UpperSampleIndex(overlay.track, timestamp)};
}

}  // namespace racevideo
