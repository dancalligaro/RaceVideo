#include "ffmpeg/video_probe.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "ffmpeg/process.h"

namespace racevideo {
namespace {

struct StreamDetails {
  std::string codec_type;
  std::string codec_name;
  int width = 0;
  int height = 0;
  std::string pixel_format;
  std::string average_frame_rate;
  std::string real_frame_rate;
  std::string time_base;
  std::string rotation = "0";
  std::string color_space;
  std::string color_transfer;
  std::string color_primaries;
  std::string sample_rate;
  int channels = 0;
  std::string channel_layout;
};

std::string Unquote(std::string_view value) {
  value = absl::StripAsciiWhitespace(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value.remove_prefix(1);
    value.remove_suffix(1);
  }
  return std::string(value);
}

absl::StatusOr<double> ParseFrameRate(std::string_view value) {
  const std::vector<std::string_view> parts = absl::StrSplit(value, '/');
  if (parts.size() != 2) {
    return absl::DataLossError("ffprobe returned an invalid frame rate");
  }
  double numerator = 0.0;
  double denominator = 0.0;
  if (!absl::SimpleAtod(parts[0], &numerator) ||
      !absl::SimpleAtod(parts[1], &denominator) || denominator == 0.0) {
    return absl::DataLossError("ffprobe returned an invalid frame rate");
  }
  const double result = numerator / denominator;
  if (!std::isfinite(result) || result <= 0.0 || result > 1000.0) {
    return absl::DataLossError("ffprobe returned an unsupported frame rate");
  }
  return result;
}

std::string PathAsUtf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

}  // namespace

absl::StatusOr<VideoInfo> ParseFfprobeOutput(std::string_view output) {
  double duration = 0.0;
  std::vector<StreamDetails> streams;
  for (std::string_view line : absl::StrSplit(output, '\n')) {
    line = absl::StripAsciiWhitespace(line);
    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, separator);
    const std::string value = Unquote(line.substr(separator + 1));
    if (key == "format.duration") {
      if (!absl::SimpleAtod(value, &duration)) {
        return absl::DataLossError("ffprobe returned an invalid duration");
      }
      continue;
    }
    constexpr std::string_view kStreamPrefix = "streams.stream.";
    if (!key.starts_with(kStreamPrefix)) continue;
    const std::string_view indexed_field = key.substr(kStreamPrefix.size());
    const std::size_t index_end = indexed_field.find('.');
    if (index_end == std::string_view::npos) continue;
    int index = -1;
    if (!absl::SimpleAtoi(indexed_field.substr(0, index_end), &index) ||
        index < 0 || index > 64) {
      return absl::DataLossError("ffprobe returned an invalid stream index");
    }
    if (streams.size() <= static_cast<std::size_t>(index)) {
      streams.resize(static_cast<std::size_t>(index) + 1);
    }
    StreamDetails& stream = streams[static_cast<std::size_t>(index)];
    const std::string_view field = indexed_field.substr(index_end + 1);
    if (field == "codec_type") {
      stream.codec_type = value;
    } else if (field == "codec_name") {
      stream.codec_name = value;
    } else if (field == "width") {
      if (!absl::SimpleAtoi(value, &stream.width)) {
        return absl::DataLossError("ffprobe returned an invalid video width");
      }
    } else if (field == "height") {
      if (!absl::SimpleAtoi(value, &stream.height)) {
        return absl::DataLossError("ffprobe returned an invalid video height");
      }
    } else if (field == "pix_fmt") {
      stream.pixel_format = value;
    } else if (field == "avg_frame_rate") {
      stream.average_frame_rate = value;
    } else if (field == "r_frame_rate") {
      stream.real_frame_rate = value;
    } else if (field == "time_base") {
      stream.time_base = value;
    } else if (field.ends_with("rotation") || field.ends_with("tags.rotate")) {
      stream.rotation = value;
    } else if (field == "color_space") {
      stream.color_space = value;
    } else if (field == "color_transfer") {
      stream.color_transfer = value;
    } else if (field == "color_primaries") {
      stream.color_primaries = value;
    } else if (field == "sample_rate") {
      stream.sample_rate = value;
    } else if (field == "channels") {
      if (!absl::SimpleAtoi(value, &stream.channels)) {
        return absl::DataLossError("ffprobe returned invalid audio channels");
      }
    } else if (field == "channel_layout") {
      stream.channel_layout = value;
    }
  }
  const StreamDetails* video = nullptr;
  const StreamDetails* audio = nullptr;
  for (const StreamDetails& stream : streams) {
    if (video == nullptr && stream.codec_type == "video") video = &stream;
    if (audio == nullptr && stream.codec_type == "audio") audio = &stream;
  }
  if (video == nullptr || video->width <= 0 || video->width > 32768 ||
      video->height <= 0 || video->height > 32768) {
    return absl::DataLossError(
        "ffprobe did not return a supported primary video stream");
  }
  if (!std::isfinite(duration) || duration <= 0.0) {
    return absl::DataLossError("ffprobe did not return a positive duration");
  }
  absl::StatusOr<double> frames_per_second =
      ParseFrameRate(video->average_frame_rate);
  if (!frames_per_second.ok()) {
    frames_per_second = ParseFrameRate(video->real_frame_rate);
  }
  if (!frames_per_second.ok()) return frames_per_second.status();
  return VideoInfo{.width = video->width,
                   .height = video->height,
                   .frames_per_second = *frames_per_second,
                   .duration_seconds = duration,
                   .has_audio = audio != nullptr,
                   .video_codec = video->codec_name,
                   .pixel_format = video->pixel_format,
                   .video_time_base = video->time_base,
                   .rotation = video->rotation,
                   .color_space = video->color_space,
                   .color_transfer = video->color_transfer,
                   .color_primaries = video->color_primaries,
                   .audio_codec = audio == nullptr ? "" : audio->codec_name,
                   .audio_sample_rate =
                       audio == nullptr ? "" : audio->sample_rate,
                   .audio_channels = audio == nullptr ? 0 : audio->channels,
                   .audio_channel_layout =
                       audio == nullptr ? "" : audio->channel_layout,
                   .audio_time_base = audio == nullptr ? "" : audio->time_base};
}

absl::StatusOr<VideoInfo> ProbeVideo(
    const std::filesystem::path& input_path) {
  absl::StatusOr<std::filesystem::path> ffprobe =
      FindExecutableOnPath("ffprobe");
  if (!ffprobe.ok()) return ffprobe.status();
  const std::vector<std::string> arguments = {
      "-hide_banner",
      "-v",
      "error",
      "-show_entries",
      "stream=codec_type,codec_name,width,height,pix_fmt,avg_frame_rate,"
      "r_frame_rate,time_base,color_space,color_transfer,color_primaries,"
      "sample_rate,channels,channel_layout:stream_tags=rotate:"
      "stream_side_data=rotation:format=duration",
      "-of",
      "flat",
      PathAsUtf8(input_path)};
  absl::StatusOr<ProcessResult> process =
      RunProcessAndCaptureOutput(*ffprobe, arguments);
  if (!process.ok()) return process.status();
  if (process->exit_code != 0) {
    return absl::DataLossError(absl::StrCat(
        "ffprobe failed with exit code ", process->exit_code, ": ",
        process->output));
  }
  return ParseFfprobeOutput(process->output);
}

absl::Status ValidateCompatibleVideo(const VideoInfo& expected,
                                     const VideoInfo& candidate) {
  auto mismatch = [](std::string_view property) {
    return absl::InvalidArgumentError(
        absl::StrCat("chapter does not match first input: ", property));
  };
  if (candidate.video_codec != expected.video_codec) return mismatch("codec");
  if (candidate.width != expected.width || candidate.height != expected.height) {
    return mismatch("dimensions");
  }
  if (candidate.pixel_format != expected.pixel_format) {
    return mismatch("pixel format");
  }
  if (std::abs(candidate.frames_per_second - expected.frames_per_second) >
      1e-6) {
    return mismatch("frame rate");
  }
  if (candidate.video_time_base != expected.video_time_base) {
    return mismatch("video time base");
  }
  if (candidate.rotation != expected.rotation) return mismatch("rotation");
  if (candidate.color_space != expected.color_space ||
      candidate.color_transfer != expected.color_transfer ||
      candidate.color_primaries != expected.color_primaries) {
    return mismatch("color properties");
  }
  if (candidate.has_audio != expected.has_audio) return mismatch("audio presence");
  if (expected.has_audio &&
      (candidate.audio_codec != expected.audio_codec ||
       candidate.audio_sample_rate != expected.audio_sample_rate ||
       candidate.audio_channels != expected.audio_channels ||
       candidate.audio_channel_layout != expected.audio_channel_layout ||
       candidate.audio_time_base != expected.audio_time_base)) {
    return mismatch("audio properties");
  }
  return absl::OkStatus();
}

}  // namespace racevideo
