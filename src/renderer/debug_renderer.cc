#include "renderer/debug_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "stb_image_write.h"

namespace racevideo {
namespace {

struct Color { uint8_t r; uint8_t g; uint8_t b; uint8_t a; };
constexpr Color kWhite{255, 255, 255, 230};
constexpr Color kBlue{30, 145, 255, 255};
constexpr Color kDark{8, 12, 18, 205};
constexpr Color kMuted{158, 174, 192, 230};
constexpr Color kGrid{255, 255, 255, 75};
constexpr Color kRed{255, 70, 60, 255};

class Canvas {
 public:
  Canvas(int width, int height)
      : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) *
                                                height * 4, 0) {}
  int width() const { return width_; }
  int height() const { return height_; }
  const uint8_t* data() const { return pixels_.data(); }
  uint8_t* mutable_data() { return pixels_.data(); }
  std::vector<uint8_t> TakePixels() { return std::move(pixels_); }

  void Pixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint8_t* pixel = &pixels_[(static_cast<std::size_t>(y) * width_ + x) * 4];
    const unsigned alpha = color.a;
    const unsigned inverse = 255 - alpha;
    pixel[0] = static_cast<uint8_t>((color.r * alpha + pixel[0] * inverse) / 255);
    pixel[1] = static_cast<uint8_t>((color.g * alpha + pixel[1] * inverse) / 255);
    pixel[2] = static_cast<uint8_t>((color.b * alpha + pixel[2] * inverse) / 255);
    pixel[3] = static_cast<uint8_t>(alpha + pixel[3] * inverse / 255);
  }
  void Disc(int x, int y, int radius, Color color) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy <= radius * radius) Pixel(x + dx, y + dy, color);
      }
    }
  }
  void Line(int x0, int y0, int x1, int y1, int thickness, Color color) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
      Disc(x0, y0, std::max(1, thickness / 2), color);
      if (x0 == x1 && y0 == y1) break;
      const int twice_error = 2 * error;
      if (twice_error >= dy) { error += dy; x0 += sx; }
      if (twice_error <= dx) { error += dx; y0 += sy; }
    }
  }
  void Rectangle(int x, int y, int width, int height, Color color) {
    for (int row = y; row < y + height; ++row) {
      for (int column = x; column < x + width; ++column) Pixel(column, row, color);
    }
  }
  void RectangleOutline(int x, int y, int width, int height, int thickness,
                        Color color) {
    Rectangle(x, y, width, thickness, color);
    Rectangle(x, y + height - thickness, width, thickness, color);
    Rectangle(x, y, thickness, height, color);
    Rectangle(x + width - thickness, y, thickness, height, color);
  }
  void Circle(int cx, int cy, int radius, int thickness, Color color) {
    constexpr int kSegments = 96;
    int last_x = cx + radius;
    int last_y = cy;
    for (int i = 1; i <= kSegments; ++i) {
      const double angle = 2.0 * 3.14159265358979323846 * i / kSegments;
      const int x = cx + static_cast<int>(std::lround(std::cos(angle) * radius));
      const int y = cy + static_cast<int>(std::lround(std::sin(angle) * radius));
      Line(last_x, last_y, x, y, thickness, color);
      last_x = x; last_y = y;
    }
  }

 private:
  int width_;
  int height_;
  std::vector<uint8_t> pixels_;
};

std::array<uint8_t, 7> Glyph(char character) {
  switch (character) {
    case '0': return {14, 17, 19, 21, 25, 17, 14};
    case '1': return {4, 12, 4, 4, 4, 4, 14};
    case '2': return {14, 17, 1, 2, 4, 8, 31};
    case '3': return {30, 1, 1, 14, 1, 1, 30};
    case '4': return {2, 6, 10, 18, 31, 2, 2};
    case '5': return {31, 16, 16, 30, 1, 1, 30};
    case '6': return {14, 16, 16, 30, 17, 17, 14};
    case '7': return {31, 1, 2, 4, 8, 8, 8};
    case '8': return {14, 17, 17, 14, 17, 17, 14};
    case '9': return {14, 17, 17, 15, 1, 1, 14};
    case 'A': return {14, 17, 17, 31, 17, 17, 17};
    case 'C': return {14, 17, 16, 16, 16, 17, 14};
    case 'D': return {30, 17, 17, 17, 17, 17, 30};
    case 'E': return {31, 16, 16, 30, 16, 16, 31};
    case 'F': return {31, 16, 16, 30, 16, 16, 16};
    case 'G': return {14, 17, 16, 23, 17, 17, 15};
    case 'H': return {17, 17, 17, 31, 17, 17, 17};
    case 'I': return {14, 4, 4, 4, 4, 4, 14};
    case 'K': return {17, 18, 20, 24, 20, 18, 17};
    case 'L': return {16, 16, 16, 16, 16, 16, 31};
    case 'M': return {17, 27, 21, 21, 17, 17, 17};
    case 'N': return {17, 25, 21, 19, 17, 17, 17};
    case 'O': return {14, 17, 17, 17, 17, 17, 14};
    case 'P': return {30, 17, 17, 30, 16, 16, 16};
    case 'R': return {30, 17, 17, 30, 20, 18, 17};
    case 'S': return {15, 16, 16, 14, 1, 1, 30};
    case 'T': return {31, 4, 4, 4, 4, 4, 4};
    case 'U': return {17, 17, 17, 17, 17, 17, 14};
    case 'V': return {17, 17, 17, 17, 17, 10, 4};
    case 'W': return {17, 17, 17, 21, 21, 21, 10};
    case '.': return {0, 0, 0, 0, 0, 6, 6};
    case '+': return {0, 4, 4, 31, 4, 4, 0};
    case '-': return {0, 0, 0, 31, 0, 0, 0};
    default: return {0, 0, 0, 0, 0, 0, 0};
  }
}

int TextWidth(std::string_view text, int scale) {
  return text.empty() ? 0 : static_cast<int>(text.size()) * 6 * scale - scale;
}

void DrawText(Canvas& canvas, std::string_view text, int x, int y, int scale,
              Color color) {
  for (char character : text) {
    const std::array<uint8_t, 7> glyph = Glyph(character);
    for (int row = 0; row < 7; ++row) {
      for (int column = 0; column < 5; ++column) {
        if ((glyph[static_cast<std::size_t>(row)] & (1 << (4 - column))) != 0) {
          canvas.Rectangle(x + column * scale, y + row * scale, scale, scale,
                           color);
        }
      }
    }
    x += 6 * scale;
  }
}

void DrawCenteredText(Canvas& canvas, std::string_view text, int center_x,
                      int y, int scale, Color color) {
  DrawText(canvas, text, center_x - TextWidth(text, scale) / 2, y, scale,
           color);
}

void DrawPanel(Canvas& canvas, int x, int y, int width, int height) {
  canvas.Rectangle(x + 5, y + 6, width, height, Color{0, 0, 0, 90});
  canvas.Rectangle(x, y, width, height, kDark);
  canvas.RectangleOutline(x, y, width, height, 1, Color{255, 255, 255, 45});
}

void DrawDigit(Canvas& canvas, int digit, int x, int y, int size, Color color) {
  static constexpr std::array<uint8_t, 10> kSegments = {
      0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  const uint8_t segments = kSegments[static_cast<std::size_t>(digit)];
  const int w = size * 4;
  const int h = size * 7;
  auto line = [&](int bit, int x0, int y0, int x1, int y1) {
    if ((segments & (1 << bit)) != 0) canvas.Line(x0, y0, x1, y1, size, color);
  };
  line(0, x + size, y, x + w - size, y);
  line(1, x + w, y + size, x + w, y + h / 2 - size);
  line(2, x + w, y + h / 2 + size, x + w, y + h - size);
  line(3, x + size, y + h, x + w - size, y + h);
  line(4, x, y + h / 2 + size, x, y + h - size);
  line(5, x, y + size, x, y + h / 2 - size);
  line(6, x + size, y + h / 2, x + w - size, y + h / 2);
}

void DrawNumber(Canvas& canvas, int value, int x, int y, int size,
                Color color) {
  const std::string digits = std::to_string(std::max(0, value));
  for (char digit : digits) {
    DrawDigit(canvas, digit - '0', x, y, size, color);
    x += size * 6;
  }
}

void DrawTrack(Canvas& canvas, const OverlayData& overlay,
               std::size_t explored, int x, int y, int size) {
  DrawPanel(canvas, x, y, size, size);
  const int padding = size / 14;
  const int plot_x = x + padding;
  const int plot_y = y + padding;
  const int plot_size = size - padding * 2;
  auto point = [&](std::size_t index) {
    return std::pair{
        plot_x + static_cast<int>(overlay.track[index].x * plot_size),
        plot_y + static_cast<int>(overlay.track[index].y * plot_size)};
  };
  for (std::size_t i = 1; i < overlay.track.size(); ++i) {
    const auto [x0, y0] = point(i - 1);
    const auto [x1, y1] = point(i);
    canvas.Line(x0, y0, x1, y1, std::max(2, size / 100), kWhite);
  }
  const std::size_t end = std::min(explored, overlay.track.size());
  for (std::size_t i = 1; i < end; ++i) {
    const auto [x0, y0] = point(i - 1);
    const auto [x1, y1] = point(i);
    canvas.Line(x0, y0, x1, y1, std::max(3, size / 80), kBlue);
  }
  if (end > 0) {
    const auto [px, py] = point(end - 1);
    canvas.Disc(px, py, std::max(4, size / 40), kRed);
  }
}

void DrawCompass(Canvas& canvas, double heading, int cx, int cy, int radius) {
  canvas.Circle(cx, cy, radius, 3, kWhite);
  const int text_scale = std::max(1, radius / 30);
  DrawCenteredText(canvas, "N", cx, cy - radius + text_scale * 3,
                   text_scale, kWhite);
  DrawCenteredText(canvas, "E", cx + radius - text_scale * 4,
                   cy - text_scale * 3, text_scale, kMuted);
  DrawCenteredText(canvas, "S", cx, cy + radius - text_scale * 10,
                   text_scale, kMuted);
  DrawCenteredText(canvas, "W", cx - radius + text_scale * 4,
                   cy - text_scale * 3, text_scale, kMuted);
  const double angle = (heading - 90.0) * 3.14159265358979323846 / 180.0;
  canvas.Line(cx, cy,
              cx + static_cast<int>(std::cos(angle) * radius * 0.78),
              cy + static_cast<int>(std::sin(angle) * radius * 0.78), 7,
              kBlue);
  canvas.Disc(cx, cy, 7, kWhite);
}

std::string CardinalDirection(double heading) {
  static constexpr std::array<std::string_view, 8> kDirections = {
      "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const int index = static_cast<int>(std::lround(heading / 45.0)) % 8;
  return std::string(kDirections[static_cast<std::size_t>(index)]);
}

std::string FormatG(double value) {
  std::ostringstream output;
  output << std::showpos << std::fixed << std::setprecision(1) << value;
  return output.str();
}

void DrawGForce(Canvas& canvas, const GForceReading& g, int cx, int cy,
                int radius) {
  canvas.Circle(cx, cy, radius, 3, kWhite);
  canvas.Circle(cx, cy, radius / 2, 2, kGrid);
  canvas.Line(cx - radius, cy, cx + radius, cy, 1, kGrid);
  canvas.Line(cx, cy - radius, cx, cy + radius, 1, kGrid);
  const int axis_scale = std::max(1, radius / 35);
  DrawCenteredText(canvas, "FWD", cx, cy - radius + axis_scale * 3,
                   axis_scale, kMuted);
  DrawCenteredText(canvas, "L", cx - radius + axis_scale * 4,
                   cy - axis_scale * 3, axis_scale, kMuted);
  DrawCenteredText(canvas, "R", cx + radius - axis_scale * 4,
                   cy - axis_scale * 3, axis_scale, kMuted);
  constexpr double kDisplayedG = 1.5;
  const double lateral = std::clamp(g.lateral_g / kDisplayedG, -1.0, 1.0);
  const double longitudinal =
      std::clamp(g.longitudinal_g / kDisplayedG, -1.0, 1.0);
  canvas.Disc(cx + static_cast<int>(lateral * radius),
              cy - static_cast<int>(longitudinal * radius), 10, kRed);
}

struct WriteContext { std::ofstream stream; bool failed = false; };
void WritePngBytes(void* context, void* data, int size) {
  auto* output = static_cast<WriteContext*>(context);
  output->stream.write(static_cast<const char*>(data), size);
  if (!output->stream) output->failed = true;
}

absl::Status WritePng(const std::filesystem::path& path, const Canvas& canvas) {
  WriteContext output{.stream = std::ofstream(path, std::ios::binary)};
  if (!output.stream) {
    return absl::UnknownError(absl::StrCat("cannot create ", path.string()));
  }
  const int result = stbi_write_png_to_func(WritePngBytes, &output,
                                             canvas.width(), canvas.height(),
                                             4, canvas.data(),
                                             canvas.width() * 4);
  output.stream.close();
  if (result == 0 || output.failed) {
    return absl::UnknownError(absl::StrCat("cannot write ", path.string()));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<std::uint8_t>> RenderOverlayFrameRgba(
    const TelemetryData& telemetry, const OverlayData& overlay,
    double timestamp_seconds, int width, int height, SpeedUnit speed_unit) {
  if (!std::isfinite(timestamp_seconds) || timestamp_seconds < 0.0 ||
      width < 160 || height < 90 || width > 7680 || height > 4320) {
    return absl::InvalidArgumentError(
        "overlay frame timestamp or dimensions are outside safe limits");
  }
  absl::StatusOr<OverlayFrameData> frame = SampleOverlayFrame(
      telemetry, overlay, absl::Seconds(timestamp_seconds));
  if (!frame.ok()) return frame.status();

  Canvas canvas(width, height);
  const int margin = std::max(16, height / 40);
  const int track_size = std::min(width / 3, height * 5 / 9);
  DrawTrack(canvas, overlay, frame->explored_track_point_count,
            width - track_size - margin, margin, track_size);
  const int dial_radius = std::max(38, height / 11);
  const int panel_width = dial_radius * 2 + margin;
  const int panel_height = dial_radius * 2 + margin * 3;
  const int panel_y = height - margin - panel_height;
  const int compass_x = margin;
  DrawPanel(canvas, compass_x, panel_y, panel_width, panel_height);
  const int label_scale = std::max(1, height / 360);
  DrawCompass(canvas, frame->heading_degrees, compass_x + panel_width / 2,
              panel_y + margin * 2 + dial_radius, dial_radius);
  std::ostringstream heading;
  heading << std::setfill('0') << std::setw(3)
          << static_cast<int>(std::lround(frame->heading_degrees)) % 360
          << " " << CardinalDirection(frame->heading_degrees);
  DrawCenteredText(canvas, heading.str(), compass_x + panel_width / 2,
                   panel_y + panel_height - margin, label_scale, kWhite);

  const int g_panel_x = compass_x + panel_width + margin;
  DrawPanel(canvas, g_panel_x, panel_y, panel_width, panel_height);
  DrawGForce(canvas, frame->g_force, g_panel_x + panel_width / 2,
             panel_y + margin * 2 + dial_radius, dial_radius);
  const std::string lateral_g =
      absl::StrCat("L", FormatG(frame->g_force.lateral_g));
  const std::string forward_g =
      absl::StrCat("F", FormatG(frame->g_force.longitudinal_g));
  DrawCenteredText(canvas, lateral_g, g_panel_x + panel_width / 4,
                   panel_y + panel_height - margin, label_scale, kWhite);
  DrawCenteredText(canvas, forward_g, g_panel_x + panel_width * 3 / 4,
                   panel_y + panel_height - margin, label_scale, kWhite);

  const int digit_size = std::max(3, height / 90);
  const double speed_factor =
      speed_unit == SpeedUnit::kMilesPerHour ? 2.2369362920544 : 3.6;
  const int speed = static_cast<int>(std::lround(std::clamp(
      frame->speed_meters_per_second * speed_factor, 0.0, 999.0)));
  const std::string speed_text = std::to_string(std::max(0, speed));
  const std::string_view unit =
      speed_unit == SpeedUnit::kMilesPerHour ? "MPH" : "KMH";
  const int unit_scale = std::max(1, digit_size / 2);
  const int speed_padding = digit_size * 2;
  const int number_width = static_cast<int>(speed_text.size()) * digit_size * 6;
  const int speed_width = speed_padding * 2 + number_width + digit_size +
                          TextWidth(unit, unit_scale);
  const int speed_height = digit_size * 9;
  DrawPanel(canvas, margin, margin, speed_width, speed_height);
  DrawNumber(canvas, speed, margin + speed_padding, margin + digit_size,
             digit_size, kWhite);
  const int unit_x = margin + speed_padding + number_width + digit_size;
  const int unit_y = margin + (speed_height - 7 * unit_scale) / 2;
  DrawText(canvas, unit, unit_x, unit_y, unit_scale, kMuted);
  return canvas.TakePixels();
}

absl::Status RenderDebugFrames(const TelemetryData& telemetry,
                               const OverlayData& overlay,
                               const DebugRenderOptions& options) {
  std::error_code error;
  std::filesystem::create_directories(options.output_directory, error);
  if (error) {
    return absl::UnknownError(absl::StrCat(
        "cannot create frame directory: ", error.message()));
  }
  const int frame_count = static_cast<int>(
      std::ceil(options.duration_seconds * options.frames_per_second));
  for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
    std::ostringstream name;
    name << "frame_" << std::setfill('0') << std::setw(6) << frame_index
         << ".png";
    const std::filesystem::path path = options.output_directory / name.str();
    if (std::filesystem::exists(path, error)) {
      return absl::AlreadyExistsError(
          absl::StrCat("refusing to overwrite existing frame: ", path.string()));
    }
    if (error) return absl::UnknownError(error.message());
    const double seconds = options.start_seconds +
                           frame_index / options.frames_per_second;
    absl::StatusOr<std::vector<std::uint8_t>> pixels =
        RenderOverlayFrameRgba(telemetry, overlay, seconds, options.width,
                               options.height, options.speed_unit);
    if (!pixels.ok()) return pixels.status();
    Canvas canvas(options.width, options.height);
    std::copy(pixels->begin(), pixels->end(),
              canvas.mutable_data());
    const absl::Status status = WritePng(path, canvas);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

}  // namespace racevideo
