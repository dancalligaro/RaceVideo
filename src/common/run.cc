#include "common/run.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/input_list.h"
#include "ffmpeg/video_encoder.h"
#include "ffmpeg/video_probe.h"
#include "parser/gpmf_metadata.h"
#include "parser/mp4_gpmf.h"
#include "parser/telemetry_decoder.h"
#include "parser/telemetry_protobuf.h"
#include "overlay/overlay_data.h"
#include "renderer/debug_renderer.h"
#include "statistics/gps_statistics.h"
#include "telemetry/axis_mapping.h"
#include "telemetry/g_force.h"
#include "telemetry/mount_orientation.h"

namespace racevideo {
namespace {

constexpr double kMetersPerSecondToKilometersPerHour = 3.6;
constexpr double kMetersPerKilometer = 1000.0;

std::string FormatDuration(double total_seconds) {
  const int minutes = static_cast<int>(total_seconds / 60.0);
  const double seconds = total_seconds - minutes * 60.0;
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << total_seconds
         << " seconds (" << minutes << " minutes " << seconds
         << " seconds)";
  return output.str();
}

void PrintFineMountCalibration(const FineMountCalibration& calibration) {
  if (!calibration.applied) {
    std::cout << "Automatic mount calibration unavailable; using orthogonal "
                 "orientation only.\n";
    return;
  }
  std::ostringstream message;
  message << std::fixed << std::setprecision(2)
          << "Automatic mount calibration: pitch "
          << calibration.pitch_degrees << " degrees, roll "
          << calibration.roll_degrees << " degrees ("
          << calibration.stationary_samples << " stationary samples).\n";
  std::cout << message.str();
}

absl::Status ValidateInputFile(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return absl::UnknownError(absl::StrCat(
          "cannot inspect input path: ", path.string(), ": ", error.message()));
    }
    return absl::NotFoundError(
        absl::StrCat("input file not found: ", path.string()));
  }
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return absl::InvalidArgumentError(
        absl::StrCat("input is not a regular file: ", path.string()));
  }
  return absl::OkStatus();
}

template <typename Value>
absl::Status AppendSamples(const std::vector<TimedSample<Value>>& source,
                           absl::Duration offset,
                           absl::Duration chapter_duration,
                           std::vector<TimedSample<Value>>* destination) {
  for (const TimedSample<Value>& sample : source) {
    if (sample.timestamp < absl::ZeroDuration() ||
        sample.timestamp > chapter_duration) {
      continue;
    }
    TimedSample<Value> adjusted = sample;
    adjusted.timestamp += offset;
    if (!destination->empty() &&
        adjusted.timestamp <= destination->back().timestamp) {
      if (adjusted.timestamp == destination->back().timestamp) continue;
      return absl::DataLossError(
          "chapter telemetry timestamps are not increasing");
    }
    destination->push_back(adjusted);
  }
  return absl::OkStatus();
}

struct CombinedChapters {
  std::vector<VideoChapter> chapters;
  VideoInfo video;
  TelemetryData telemetry;
};

absl::StatusOr<CombinedChapters> PrepareChapters(
    const std::vector<std::filesystem::path>& paths,
    std::string_view imu_axis_order) {
  CombinedChapters combined;
  double timeline_seconds = 0.0;
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const std::filesystem::path& path = paths[index];
    std::cout << "Scanning chapter " << index + 1 << '/' << paths.size()
              << ": " << path.filename().string() << "...\n"
              << std::flush;
    absl::Status status = ValidateInputFile(path);
    if (!status.ok()) return status;
    absl::StatusOr<VideoInfo> video = ProbeVideo(path);
    if (!video.ok()) return video.status();
    if (index == 0) {
      combined.video = *video;
    } else {
      status = ValidateCompatibleVideo(combined.video, *video);
      if (!status.ok()) {
        return absl::InvalidArgumentError(absl::StrCat(
            path.string(), ": ", status.message()));
      }
    }
    absl::StatusOr<GpmfTrackInfo> track = IndexGpmfTrack(path);
    if (!track.ok()) return track.status();
    absl::StatusOr<TelemetryData> telemetry = DecodeTelemetry(path, *track);
    if (!telemetry.ok()) return telemetry.status();
    status = NormalizeInertialAxes(imu_axis_order, &*telemetry);
    if (!status.ok()) return status;
    status = DetectAndApplyMountOrientation(&*telemetry);
    if (!status.ok()) return status;
    if (index == 0) {
      combined.telemetry.acceleration_metadata =
          telemetry->acceleration_metadata;
      combined.telemetry.angular_velocity_metadata =
          telemetry->angular_velocity_metadata;
    } else if (telemetry->acceleration_metadata.mount_orientation !=
               combined.telemetry.acceleration_metadata.mount_orientation) {
      return absl::FailedPreconditionError(absl::StrCat(
          path.string(), ": detected mount orientation differs from the "
                         "first chapter"));
    }
    const absl::Duration offset = absl::Seconds(timeline_seconds);
    const absl::Duration chapter_duration =
        absl::Seconds(video->duration_seconds);
    status = AppendSamples(telemetry->gps, offset, chapter_duration,
                           &combined.telemetry.gps);
    if (!status.ok()) return status;
    status = AppendSamples(
        telemetry->acceleration_meters_per_second_squared, offset,
        chapter_duration,
        &combined.telemetry.acceleration_meters_per_second_squared);
    if (!status.ok()) return status;
    status = AppendSamples(telemetry->angular_velocity_radians_per_second,
                           offset, chapter_duration,
                           &combined.telemetry
                                .angular_velocity_radians_per_second);
    if (!status.ok()) return status;
    combined.chapters.push_back(
        {.path = path, .duration_seconds = video->duration_seconds});
    timeline_seconds += video->duration_seconds;
    std::cout << "Scanned chapter " << index + 1 << '/' << paths.size()
              << ".\n";
  }
  combined.video.duration_seconds = timeline_seconds;
  const FineMountCalibration calibration =
      ApplyStationaryMountCalibration(&combined.telemetry);
  PrintFineMountCalibration(calibration);
  absl::Status status = GenerateFilteredGForce(
      kDefaultGForceFilterCutoffHz, &combined.telemetry);
  if (!status.ok()) return status;
  return combined;
}

absl::Status RunChapterList(const Options& options) {
  absl::StatusOr<std::vector<std::filesystem::path>> paths =
      ReadInputList(options.input_list_path);
  if (!paths.ok()) return paths.status();
  absl::StatusOr<CombinedChapters> combined =
      PrepareChapters(*paths, options.imu_axis_order);
  if (!combined.ok()) return combined.status();
  if (options.start_seconds >= combined->video.duration_seconds) {
    return absl::OutOfRangeError(
        "start is at or beyond the combined video duration");
  }
  const double available_duration =
      combined->video.duration_seconds - options.start_seconds;
  const double actual_duration =
      options.duration_seconds == 0.0
          ? available_duration
          : std::min(options.duration_seconds, available_duration);
  std::cout << "Input chapters: " << combined->chapters.size() << '\n'
            << "Combined input duration: "
            << FormatDuration(combined->video.duration_seconds) << '\n'
            << "Selected output duration: " << FormatDuration(actual_duration)
            << '\n';
  absl::StatusOr<OverlayData> overlay = BuildOverlayData(
      combined->telemetry, absl::Seconds(options.start_seconds),
      absl::Seconds(options.start_seconds + actual_duration));
  if (!overlay.ok()) return overlay.status();

  if (!options.render_frames_path.empty()) {
    absl::Status status = RenderDebugFrames(
        combined->telemetry, *overlay,
        {.output_directory = options.render_frames_path,
         .start_seconds = options.start_seconds,
         .duration_seconds = actual_duration,
         .frames_per_second = options.render_fps,
         .width = options.render_width,
         .height = options.render_height,
         .speed_units = options.speed_units});
    if (!status.ok()) return status;
    std::cout << "Debug overlay frames written to: "
              << options.render_frames_path.string() << '\n';
  }
  if (!options.output_video_path.empty()) {
    std::error_code error;
    if (std::filesystem::exists(options.output_video_path, error)) {
      return absl::AlreadyExistsError(absl::StrCat(
          "refusing to overwrite output video: ",
          options.output_video_path.string()));
    }
    if (error) {
      return absl::UnknownError(absl::StrCat(
          "cannot inspect output video path: ", error.message()));
    }
    absl::Status status = EncodeOverlayVideo(
        combined->telemetry, *overlay, combined->video,
        {.chapters = combined->chapters,
         .output_path = options.output_video_path,
         .start_seconds = options.start_seconds,
         .duration_seconds = actual_duration,
         .output_width = options.output_width,
         .video_encoder = options.video_encoder,
         .video_pipeline = options.video_pipeline,
         .speed_units = options.speed_units});
    if (!status.ok()) return status;
    std::cout << "Overlay video written to: "
              << options.output_video_path.string() << '\n';
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status Run(const Options& options) {
  if (!options.inspect_telemetry_path.empty()) {
    absl::StatusOr<TelemetryData> telemetry =
        ReadTelemetryProtobuf(options.inspect_telemetry_path);
    if (!telemetry.ok()) return telemetry.status();
    std::cout << "GPS samples: " << telemetry->gps.size() << '\n'
              << "Accelerometer samples: "
              << telemetry->acceleration_meters_per_second_squared.size()
              << '\n'
              << "Gyroscope samples: "
              << telemetry->angular_velocity_radians_per_second.size() << '\n'
              << "IMU coordinate frame: "
              << (telemetry->acceleration_metadata.values_are_forward_upright
                      ? "forward-facing upright camera XYZ"
                      : telemetry->acceleration_metadata.values_are_camera_xyz
                            ? "camera XYZ"
                            : "encoded components")
              << '\n';
    if (!telemetry->acceleration_metadata.source_axis_order.empty()) {
      std::cout << "IMU source axis order: "
                << telemetry->acceleration_metadata.source_axis_order << '\n';
    }
    std::cout << "Mount orientation: "
              << MountOrientationName(
                     telemetry->acceleration_metadata.mount_orientation)
              << '\n'
              << "Mount orientation confidence: "
              << telemetry->acceleration_metadata.mount_orientation_confidence
              << '\n';
    if (!telemetry->filtered_g_force.empty()) {
      const GForceStatistics statistics =
          ComputeGForceStatistics(*telemetry);
      std::cout << "Filtered G-force samples: "
                << telemetry->filtered_g_force.size() << '\n'
                << "G-force filter cutoff: "
                << telemetry->g_force_filter_cutoff_hz << " Hz\n"
                << "Peak forward G: " << statistics.peak_forward_g << '\n'
                << "Peak braking G: " << statistics.peak_braking_g << '\n'
                << "Peak left G: " << statistics.peak_left_g << '\n'
                << "Peak right G: " << statistics.peak_right_g << '\n'
                << "Peak vertical dynamic G: "
                << statistics.peak_vertical_dynamic_g << '\n';
    }
    if (!telemetry->gps.empty()) {
      const absl::StatusOr<GpsStatistics> statistics =
          ComputeGpsStatistics(*telemetry);
      if (!statistics.ok()) return statistics.status();
      std::cout << "Distance: "
                << statistics->distance_meters / kMetersPerKilometer
                << " km\n"
                << "Moving time: " << statistics->moving_time_seconds
                << " seconds\n"
                << "Maximum speed: "
                << statistics->maximum_speed_meters_per_second *
                       kMetersPerSecondToKilometersPerHour
                << " km/h\n"
                << "Average moving speed: "
                << statistics->average_moving_speed_meters_per_second *
                       kMetersPerSecondToKilometersPerHour
                << " km/h\n"
                << "Elevation gain: " << statistics->elevation_gain_meters
                << " meters\n";
    }
    return absl::OkStatus();
  }

  if (!options.input_list_path.empty()) return RunChapterList(options);

  std::error_code error;
  const bool exists = std::filesystem::exists(options.input_path, error);
  if (error) {
    return absl::UnknownError(absl::StrCat(
        "cannot inspect input path: ", options.input_path.string(), ": ",
        error.message()));
  }
  if (!exists) {
    return absl::NotFoundError(
        absl::StrCat("input file not found: ", options.input_path.string()));
  }

  const bool is_regular_file =
      std::filesystem::is_regular_file(options.input_path, error);
  if (error) {
    return absl::UnknownError(absl::StrCat(
        "cannot inspect input path: ", options.input_path.string(), ": ",
        error.message()));
  }
  if (!is_regular_file) {
    return absl::InvalidArgumentError(
        absl::StrCat("input is not a file: ", options.input_path.string()));
  }

  std::cout << "Scanning input video: "
            << options.input_path.filename().string() << "...\n"
            << std::flush;
  absl::StatusOr<GpmfTrackInfo> track = IndexGpmfTrack(options.input_path);
  if (!track.ok()) return track.status();

  const GpmfPayload& last = track->payloads.back();
  const double duration_seconds =
      static_cast<double>(last.start_time_units + last.duration_units) /
      track->timescale;
  std::cout << "GPMF payloads: " << track->payloads.size() << '\n'
            << "Timescale: " << track->timescale << " units/second\n"
            << "Duration: " << duration_seconds << " seconds\n";

  if (options.inspect_video) {
    const absl::StatusOr<VideoInfo> video = ProbeVideo(options.input_path);
    if (!video.ok()) return video.status();
    std::cout << "Video dimensions: " << video->width << 'x' << video->height
              << '\n'
              << "Video frame rate: " << video->frames_per_second << " fps\n"
              << "Video duration: " << video->duration_seconds << " seconds\n"
              << "Audio stream: " << (video->has_audio ? "yes" : "no")
              << '\n';
  }

  if (options.inspect) {
    absl::StatusOr<GpmfSummary> summary =
        InspectGpmf(options.input_path, *track);
    if (!summary.ok()) return summary.status();
    std::cout << "Metadata bytes: " << summary->total_bytes << '\n'
              << "Samples by FourCC:\n";
    for (const auto& [key, count] : summary->samples_by_key) {
      std::cout << "  " << key << ": " << count << '\n';
    }
  }

  if (!options.extract_gpmf_path.empty()) {
    absl::Status status = ExtractRawGpmf(
        options.input_path, *track, options.extract_gpmf_path);
    if (!status.ok()) return status;
    std::cout << "Raw GPMF written to: "
              << options.extract_gpmf_path.string() << '\n';
  }

  if (!options.export_json_path.empty()) {
    std::error_code equivalent_error;
    if (std::filesystem::equivalent(options.input_path,
                                    options.export_json_path,
                                    equivalent_error) &&
        !equivalent_error) {
      return absl::InvalidArgumentError(
          "JSON output path must not overwrite the input video");
    }
    absl::StatusOr<TelemetryData> telemetry =
        DecodeTelemetry(options.input_path, *track);
    if (!telemetry.ok()) return telemetry.status();
    if (!options.imu_axis_order.empty()) {
      const absl::Status normalize_status =
          NormalizeInertialAxes(options.imu_axis_order, &*telemetry);
      if (!normalize_status.ok()) return normalize_status;
      const absl::Status mount_status =
          DetectAndApplyMountOrientation(&*telemetry);
      if (!mount_status.ok()) return mount_status;
      PrintFineMountCalibration(
          ApplyStationaryMountCalibration(&*telemetry));
      const absl::Status g_force_status = GenerateFilteredGForce(
          kDefaultGForceFilterCutoffHz, &*telemetry);
      if (!g_force_status.ok()) return g_force_status;
    }
    absl::Status status =
        WriteTelemetryJson(*telemetry, options.export_json_path);
    if (!status.ok()) return status;
    std::cout << "GPS samples: " << telemetry->gps.size() << '\n'
              << "Accelerometer samples: "
              << telemetry->acceleration_meters_per_second_squared.size()
              << '\n'
              << "Gyroscope samples: "
              << telemetry->angular_velocity_radians_per_second.size() << '\n'
              << "Telemetry JSON written to: "
              << options.export_json_path.string() << '\n';
  }

  if (!options.export_telemetry_path.empty()) {
    std::error_code equivalent_error;
    if (std::filesystem::equivalent(options.input_path,
                                    options.export_telemetry_path,
                                    equivalent_error) &&
        !equivalent_error) {
      return absl::InvalidArgumentError(
          "telemetry output path must not overwrite the input video");
    }
    absl::StatusOr<TelemetryData> telemetry =
        DecodeTelemetry(options.input_path, *track);
    if (!telemetry.ok()) return telemetry.status();
    if (!options.imu_axis_order.empty()) {
      const absl::Status normalize_status =
          NormalizeInertialAxes(options.imu_axis_order, &*telemetry);
      if (!normalize_status.ok()) return normalize_status;
      const absl::Status mount_status =
          DetectAndApplyMountOrientation(&*telemetry);
      if (!mount_status.ok()) return mount_status;
      PrintFineMountCalibration(
          ApplyStationaryMountCalibration(&*telemetry));
      const absl::Status g_force_status = GenerateFilteredGForce(
          kDefaultGForceFilterCutoffHz, &*telemetry);
      if (!g_force_status.ok()) return g_force_status;
    }
    const absl::Status status =
        WriteTelemetryProtobuf(*telemetry, options.export_telemetry_path);
    if (!status.ok()) return status;
    std::cout << "GPS samples: " << telemetry->gps.size() << '\n'
              << "Accelerometer samples: "
              << telemetry->acceleration_meters_per_second_squared.size()
              << '\n'
              << "Gyroscope samples: "
              << telemetry->angular_velocity_radians_per_second.size() << '\n'
              << "Telemetry Protobuf written to: "
              << options.export_telemetry_path.string() << '\n';
  }

  if (!options.render_frames_path.empty()) {
    if (options.imu_axis_order.empty()) {
      return absl::InvalidArgumentError(
          "--render_frames requires --imu_axis_order so acceleration can be "
          "mapped to vehicle axes");
    }
    if (options.start_seconds >= duration_seconds) {
      return absl::OutOfRangeError(
          "render start is at or beyond the metadata duration");
    }
    absl::StatusOr<TelemetryData> telemetry =
        DecodeTelemetry(options.input_path, *track);
    if (!telemetry.ok()) return telemetry.status();
    absl::Status status =
        NormalizeInertialAxes(options.imu_axis_order, &*telemetry);
    if (!status.ok()) return status;
    status = DetectAndApplyMountOrientation(&*telemetry);
    if (!status.ok()) return status;
    PrintFineMountCalibration(ApplyStationaryMountCalibration(&*telemetry));
    status = GenerateFilteredGForce(kDefaultGForceFilterCutoffHz, &*telemetry);
    if (!status.ok()) return status;
    const double available_duration = duration_seconds - options.start_seconds;
    const double actual_duration =
        std::min(options.duration_seconds, available_duration);
    absl::StatusOr<OverlayData> overlay = BuildOverlayData(
        *telemetry, absl::Seconds(options.start_seconds),
        absl::Seconds(options.start_seconds + actual_duration));
    if (!overlay.ok()) return overlay.status();
    status = RenderDebugFrames(
        *telemetry, *overlay,
        {.output_directory = options.render_frames_path,
         .start_seconds = options.start_seconds,
         .duration_seconds = actual_duration,
         .frames_per_second = options.render_fps,
         .width = options.render_width,
         .height = options.render_height,
         .speed_units = options.speed_units});
    if (!status.ok()) return status;
    std::cout << "Debug overlay frames written to: "
              << options.render_frames_path.string() << '\n';
  }
  if (!options.output_video_path.empty()) {
    std::error_code output_error;
    if (std::filesystem::exists(options.output_video_path, output_error)) {
      return absl::AlreadyExistsError(absl::StrCat(
          "refusing to overwrite output video: ",
          options.output_video_path.string()));
    }
    if (output_error) {
      return absl::UnknownError(absl::StrCat(
          "cannot inspect output video path: ", output_error.message()));
    }
    if (options.start_seconds >= duration_seconds) {
      return absl::OutOfRangeError(
          "video render start is at or beyond the metadata duration");
    }
    absl::StatusOr<VideoInfo> video = ProbeVideo(options.input_path);
    if (!video.ok()) return video.status();
    const double available_duration =
        std::min(duration_seconds, video->duration_seconds) -
        options.start_seconds;
    const double actual_duration =
        options.duration_seconds == 0.0
            ? available_duration
            : std::min(options.duration_seconds, available_duration);
    if (actual_duration <= 0.0) {
      return absl::OutOfRangeError("video render range is empty");
    }
    std::cout << "Input video duration: "
              << FormatDuration(video->duration_seconds) << '\n'
              << "Selected output duration: "
              << FormatDuration(actual_duration) << '\n';
    absl::StatusOr<TelemetryData> telemetry =
        DecodeTelemetry(options.input_path, *track);
    if (!telemetry.ok()) return telemetry.status();
    absl::Status status =
        NormalizeInertialAxes(options.imu_axis_order, &*telemetry);
    if (!status.ok()) return status;
    status = DetectAndApplyMountOrientation(&*telemetry);
    if (!status.ok()) return status;
    PrintFineMountCalibration(ApplyStationaryMountCalibration(&*telemetry));
    status = GenerateFilteredGForce(kDefaultGForceFilterCutoffHz, &*telemetry);
    if (!status.ok()) return status;
    absl::StatusOr<OverlayData> overlay = BuildOverlayData(
        *telemetry, absl::Seconds(options.start_seconds),
        absl::Seconds(options.start_seconds + actual_duration));
    if (!overlay.ok()) return overlay.status();
    status = EncodeOverlayVideo(
        *telemetry, *overlay, *video,
        {.chapters = {{.path = options.input_path,
                       .duration_seconds = video->duration_seconds}},
         .output_path = options.output_video_path,
         .start_seconds = options.start_seconds,
         .duration_seconds = actual_duration,
         .output_width = options.output_width,
         .video_encoder = options.video_encoder,
         .video_pipeline = options.video_pipeline,
         .speed_units = options.speed_units});
    if (!status.ok()) return status;
    std::cout << "Overlay video written to: "
              << options.output_video_path.string() << '\n';
  }
  return absl::OkStatus();
}

}  // namespace racevideo
