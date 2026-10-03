#include "renderer/debug_renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
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
constexpr Color kMuted{158, 174, 192, 230};
constexpr Color kGrid{255, 255, 255, 75};
constexpr Color kRed{255, 70, 60, 255};
constexpr Color kShadow{0, 0, 0, 210};

class Canvas {
 public:
  Canvas(int width, int height)
      : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) *
                                                height * 4, 0) {}
  Canvas(int width, int height, std::vector<uint8_t> pixels)
      : width_(width), height_(height), pixels_(std::move(pixels)) {}
  int width() const { return width_; }
  int height() const { return height_; }
  const uint8_t* data() const { return pixels_.data(); }
  uint8_t* mutable_data() { return pixels_.data(); }
  std::vector<uint8_t> TakePixels() { return std::move(pixels_); }

  void Clear(int x, int y, int width, int height) {
    const int left = std::clamp(x, 0, width_);
    const int right = std::clamp(x + width, 0, width_);
    for (int row = std::max(0, y); row < std::min(height_, y + height); ++row) {
      std::fill_n(
          pixels_.data() + (static_cast<std::size_t>(row) * width_ + left) * 4,
          static_cast<std::size_t>(std::max(0, right - left)) * 4, 0);
    }
  }

  std::vector<uint8_t> CopyRegion(int x, int y, int width, int height,
                                  std::vector<uint8_t> result = {}) const {
    result.resize(static_cast<std::size_t>(width) * height * 4);
    for (int row = 0; row < height; ++row) {
      std::copy_n(
          pixels_.data() + (static_cast<std::size_t>(y + row) * width_ + x) * 4,
          static_cast<std::size_t>(width) * 4,
          result.data() + static_cast<std::size_t>(row) * width * 4);
    }
    return result;
  }

  void Blit(const std::vector<uint8_t>& source, int source_width,
            int source_height, int destination_x, int destination_y) {
    for (int y = 0; y < source_height; ++y) {
      const std::size_t source_offset =
          static_cast<std::size_t>(y) * source_width * 4;
      const std::size_t destination_offset =
          (static_cast<std::size_t>(destination_y + y) * width_ +
           destination_x) * 4;
      std::copy_n(source.data() + source_offset,
                  static_cast<std::size_t>(source_width) * 4,
                  pixels_.data() + destination_offset);
    }
  }

  void RestoreTrack(const TrackFrameSnapshot& track, int x, int y, int radius) {
    const int left = std::max(0, x - radius);
    const int right = std::min(track.size, x + radius + 1);
    for (int row = std::max(0, y - radius);
         row < std::min(track.size, y + radius + 1); ++row) {
      std::copy_n(track.pixels->data() +
                      (static_cast<std::size_t>(row) * track.size + left) * 4,
                  static_cast<std::size_t>(std::max(0, right - left)) * 4,
                  pixels_.data() +
                      (static_cast<std::size_t>(track.frame_y + row) * width_ +
                       track.frame_x + left) *
                          4);
    }
  }

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
  void Triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                Color color) {
    const int minimum_x = std::max(0, std::min({x0, x1, x2}));
    const int maximum_x = std::min(width_ - 1, std::max({x0, x1, x2}));
    const int minimum_y = std::max(0, std::min({y0, y1, y2}));
    const int maximum_y = std::min(height_ - 1, std::max({y0, y1, y2}));
    auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
      return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };
    for (int y = minimum_y; y <= maximum_y; ++y) {
      for (int x = minimum_x; x <= maximum_x; ++x) {
        const int first = edge(x0, y0, x1, y1, x, y);
        const int second = edge(x1, y1, x2, y2, x, y);
        const int third = edge(x2, y2, x0, y0, x, y);
        if ((first >= 0 && second >= 0 && third >= 0) ||
            (first <= 0 && second <= 0 && third <= 0)) {
          Pixel(x, y, color);
        }
      }
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
    case 'g': return {0, 0, 15, 17, 15, 1, 14};
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

void DrawOutlinedText(Canvas& canvas, std::string_view text, int x, int y,
                      int scale, Color color) {
  const int radius = std::max(2, scale / 3);
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= radius * radius) {
        DrawText(canvas, text, x + dx, y + dy, scale, kShadow);
      }
    }
  }
  DrawText(canvas, text, x, y, scale, color);
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

void DrawOutlinedNumber(Canvas& canvas, int value, int x, int y, int size,
                        Color color) {
  const int radius = std::max(2, size / 8);
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= radius * radius) {
        DrawNumber(canvas, value, x + dx, y + dy, size, kShadow);
      }
    }
  }
  DrawNumber(canvas, value, x, y, size, color);
}

void DrawArrow(Canvas& canvas, int px, int py, double heading_degrees,
               int size) {
  const double heading_radians =
      heading_degrees * 3.14159265358979323846 / 180.0;
  const double direction_x = std::sin(heading_radians);
  const double direction_y = -std::cos(heading_radians);
  const double perpendicular_x = -direction_y;
  const double perpendicular_y = direction_x;
  const int arrow_size = std::max(7, size / 22);
  const int tip_x =
      px + static_cast<int>(std::lround(direction_x * arrow_size));
  const int tip_y =
      py + static_cast<int>(std::lround(direction_y * arrow_size));
  const double base_x = px - direction_x * arrow_size * 0.45;
  const double base_y = py - direction_y * arrow_size * 0.45;
  const int left_x = static_cast<int>(
      std::lround(base_x + perpendicular_x * arrow_size * 0.65));
  const int left_y = static_cast<int>(
      std::lround(base_y + perpendicular_y * arrow_size * 0.65));
  const int right_x = static_cast<int>(
      std::lround(base_x - perpendicular_x * arrow_size * 0.65));
  const int right_y = static_cast<int>(
      std::lround(base_y - perpendicular_y * arrow_size * 0.65));
  canvas.Triangle(tip_x, tip_y, left_x, left_y, right_x, right_y, kRed);
}

void DrawTrack(Canvas& canvas, const OverlayData& overlay,
               std::size_t explored, double heading_degrees, int x, int y,
               int size) {
  const int padding = size / 14;
  const int plot_x = x + padding;
  const int plot_y = y + padding;
  const int plot_size = size - padding * 2;
  const int track_thickness = std::max(5, size / 35);
  const int shadow_thickness = track_thickness + std::max(4, size / 50);
  auto point = [&](std::size_t index) {
    return std::pair{
        plot_x + static_cast<int>(overlay.track[index].x * plot_size),
        plot_y + static_cast<int>(overlay.track[index].y * plot_size)};
  };
  for (std::size_t i = 1; i < overlay.track.size(); ++i) {
    const auto [x0, y0] = point(i - 1);
    const auto [x1, y1] = point(i);
    canvas.Line(x0, y0, x1, y1, shadow_thickness, kShadow);
  }
  for (std::size_t i = 1; i < overlay.track.size(); ++i) {
    const auto [x0, y0] = point(i - 1);
    const auto [x1, y1] = point(i);
    canvas.Line(x0, y0, x1, y1, track_thickness, kWhite);
  }
  const std::size_t end = std::min(explored, overlay.track.size());
  for (std::size_t i = 1; i < end; ++i) {
    const auto [x0, y0] = point(i - 1);
    const auto [x1, y1] = point(i);
    canvas.Line(x0, y0, x1, y1, std::max(3, size / 70), kBlue);
  }
  if (end > 0) {
    const auto [px, py] = point(end - 1);
    DrawArrow(canvas, px, py, heading_degrees, size);
  }
}

std::string FormatGMagnitude(const GForceReading& value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2)
         << std::hypot(value.lateral_g, value.longitudinal_g) << " g";
  return output.str();
}

void DrawGForceBackground(Canvas& canvas, int cx, int cy, int radius) {
  canvas.Circle(cx, cy, radius, 9, kShadow);
  canvas.Circle(cx, cy, radius, 3, kWhite);
  constexpr double kMaximumDisplayedG = 1.2;
  const int one_g_radius =
      static_cast<int>(std::lround(radius / kMaximumDisplayedG));
  canvas.Circle(cx, cy, one_g_radius, 2, kGrid);
  canvas.Line(cx - radius, cy, cx + radius, cy, 1, kGrid);
  canvas.Line(cx, cy - radius, cx, cy + radius, 1, kGrid);
}

void DrawGForceDot(Canvas& canvas, const GForceReading& g, int cx, int cy,
                   int radius) {
  constexpr double kMaximumDisplayedG = 1.2;
  const double magnitude = std::hypot(g.lateral_g, g.longitudinal_g);
  const double displayed_magnitude = std::min(magnitude, kMaximumDisplayedG);
  const double position_scale =
      magnitude > 0.0 ? displayed_magnitude / magnitude / kMaximumDisplayedG
                      : 0.0;
  const int dot_x = cx +
                    static_cast<int>(std::lround(
                        g.lateral_g * position_scale * radius));
  const int dot_y = cy -
                    static_cast<int>(std::lround(
                        g.longitudinal_g * position_scale * radius));
  canvas.Disc(dot_x, dot_y, std::max(8, radius / 10), kRed);
}

void DrawMagnitude(Canvas& canvas, std::string_view magnitude, int cx, int y,
                   int scale) {
  DrawOutlinedText(canvas, magnitude, cx - TextWidth(magnitude, scale) / 2, y,
                   scale, kWhite);
}

void DrawReadouts(Canvas& canvas, const OverlayFrameData& frame,
                  const std::vector<SpeedUnit>& speed_units,
                  bool draw_gauge = true, bool draw_speed = true,
                  bool draw_background = true) {
  const int height = canvas.height();
  const int margin = std::max(16, height / 40);
  const int dial_radius = std::max(30, height * 4 / 55);
  const int label_scale = std::max(1, height / 360);
  const int magnitude_y = height - margin - 7 * label_scale;
  const int gauge_center_x = margin + dial_radius;
  const int gauge_center_y = magnitude_y - margin / 2 - dial_radius;
  if (draw_gauge) {
    if (draw_background) {
      DrawGForceBackground(canvas, gauge_center_x, gauge_center_y, dial_radius);
    }
    DrawGForceDot(canvas, frame.g_force, gauge_center_x, gauge_center_y,
                  dial_radius);
    const std::string magnitude = FormatGMagnitude(frame.g_force);
    DrawMagnitude(canvas, magnitude, gauge_center_x, magnitude_y, label_scale);
  }

  if (!draw_speed) return;

  const int digit_size = std::max(2, std::max(3, height / 90) * 7 / 10);
  const int unit_scale = std::max(1, digit_size / 2);
  const int speed_padding = digit_size * 2;
  constexpr int kSpeedDigits = 3;
  const int digit_advance = digit_size * 6;
  const int number_width = kSpeedDigits * digit_advance;
  const int unit_x = margin + speed_padding + number_width + digit_size;
  const int row_height = digit_size * 7;
  const int row_gap = std::max(9, digit_size * 2);
  const auto draw_speed_row = [&](double factor, std::string_view unit,
                                  int row_y) {
    const int speed = static_cast<int>(std::lround(std::clamp(
        frame.speed_meters_per_second * factor, 0.0, 999.0)));
    const int digit_count =
        static_cast<int>(std::to_string(std::max(0, speed)).size());
    const int number_x =
        margin + speed_padding + (kSpeedDigits - digit_count) * digit_advance;
    DrawOutlinedNumber(canvas, speed, number_x, row_y, digit_size, kWhite);
    const int unit_y = row_y + (row_height - 7 * unit_scale) / 2;
    DrawOutlinedText(canvas, unit, unit_x, unit_y, unit_scale, kMuted);
  };
  for (std::size_t index = 0; index < speed_units.size(); ++index) {
    const bool miles = speed_units[index] == SpeedUnit::kMilesPerHour;
    draw_speed_row(miles ? 2.2369362920544 : 3.6,
                   miles ? "MPH" : "KMH",
                   margin + static_cast<int>(index) * (row_height + row_gap));
  }
}

struct WriteContext { std::ofstream stream; bool failed = false; };
void WritePngBytes(void* context, void* data, int size) {
  auto* output = static_cast<WriteContext*>(context);
  output->stream.write(static_cast<const char*>(data), size);
  if (!output->stream) output->failed = true;
}

absl::Status WritePng(const std::filesystem::path& path, int width, int height,
                      std::span<const std::uint8_t> pixels) {
  WriteContext output{.stream = std::ofstream(path, std::ios::binary)};
  if (!output.stream) {
    return absl::UnknownError(absl::StrCat("cannot create ", path.string()));
  }
  const int result = stbi_write_png_to_func(
      WritePngBytes, &output, width, height, 4, pixels.data(), width * 4);
  output.stream.close();
  if (result == 0 || output.failed) {
    return absl::UnknownError(absl::StrCat("cannot write ", path.string()));
  }
  return absl::OkStatus();
}

}  // namespace

struct CachedOverlayRenderer::Impl {
  int width;
  int height;
  std::vector<SpeedUnit> units;
  std::vector<uint8_t> pixels;
  std::vector<uint8_t> gauge_background;
  std::vector<uint8_t> gauge_text_background;
  std::vector<int> speeds;
  std::string magnitude;
  double lateral = 0;
  double longitudinal = 0;
  bool initialized = false;
  TrackFrameSnapshot previous_track{};
  OverlayRenderTiming timing;
};

CachedOverlayRenderer::CachedOverlayRenderer(int width, int height,
                                             std::vector<SpeedUnit> speed_units)
    : impl_(std::make_unique<Impl>()) {
  impl_->width = width;
  impl_->height = height;
  impl_->units = std::move(speed_units);
}
CachedOverlayRenderer::~CachedOverlayRenderer() = default;

std::span<const std::uint8_t> CachedOverlayRenderer::pixels() const {
  return impl_->pixels;
}
const OverlayRenderTiming& CachedOverlayRenderer::timing() const {
  return impl_->timing;
}

absl::Status CachedOverlayRenderer::Render(const OverlayFrameData& frame,
                                           const TrackFrameSnapshot& track) {
  auto& state = *impl_;
  const int width = state.width;
  const int height = state.height;
  if (width < 160 || height < 90 || width > 7680 || height > 4320 ||
      track.size <= 0 || track.size > std::min(width, height) ||
      track.frame_x < 0 || track.frame_y < 0 ||
      track.frame_x > width - track.size ||
      track.frame_y > height - track.size || !track.pixels ||
      track.pixels->size() !=
          static_cast<std::size_t>(track.size) * track.size * 4) {
    return absl::InvalidArgumentError("invalid cached overlay dimensions");
  }
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  if (state.pixels.empty()) {
    state.pixels.resize(static_cast<std::size_t>(width) * height * 4, 0);
  }
  Canvas canvas(width, height, std::move(state.pixels));
  const auto& previous = state.previous_track;
  if (previous.frame_x != track.frame_x || previous.frame_y != track.frame_y ||
      previous.size != track.size) {
    canvas.Clear(0, 0, width, height);
    state.initialized = false;
  }
  const int margin = std::max(16, height / 40);
  const int radius = std::max(30, height * 4 / 55);
  const int scale = std::max(1, height / 360);
  const int magnitude_y = height - margin - 7 * scale;
  const int cx = margin + radius;
  const int cy = magnitude_y - margin / 2 - radius;
  const int gauge_top = cy - radius - std::max(8, radius / 10) - 2;
  const int left_width = std::min(width, 2 * (margin + radius) + 16);
  const int digit_size = std::max(2, std::max(3, height / 90) * 7 / 10);
  const int unit_scale = std::max(1, digit_size / 2);
  const int speed_right = margin + 21 * digit_size +
                          TextWidth("KMH", unit_scale) +
                          std::max(2, unit_scale / 3) + 2;
  const int speed_bottom = margin +
                           static_cast<int>(state.units.size()) *
                               (digit_size * 7 + std::max(9, digit_size * 2)) +
                           digit_size;
  const std::string magnitude = FormatGMagnitude(frame.g_force);
  const int magnitude_half =
      TextWidth(magnitude, scale) / 2 + std::max(2, scale / 3) + 2;
  // Conservatively fall back when widget rectangles could overlap. This also
  // preserves the original clipping and draw order on very small canvases.
  const int arrow_radius = std::max(7, track.size / 22) + 2;
  const bool separate =
      gauge_top > 0 &&
      (!track.has_arrow ||
       (track.arrow_x >= arrow_radius && track.arrow_y >= arrow_radius &&
        track.arrow_x + arrow_radius < track.size &&
        track.arrow_y + arrow_radius < track.size)) &&
      left_width <= track.frame_x && cx - magnitude_half >= 0 &&
      cx + magnitude_half < left_width &&
      (state.units.empty() ||
       (speed_right <= track.frame_x && speed_bottom < gauge_top));
  if (!separate) canvas.Clear(0, 0, width, height);
  const auto buffer_end = Clock::now();
  if (separate && state.initialized && previous.pixels == track.pixels) {
    if (previous.has_arrow) {
      canvas.RestoreTrack(track, previous.arrow_x, previous.arrow_y,
                          arrow_radius);
    }
  } else {
    canvas.Blit(*track.pixels, track.size, track.size, track.frame_x,
                track.frame_y);
  }
  if (track.has_arrow) {
    DrawArrow(canvas, track.frame_x + track.arrow_x,
              track.frame_y + track.arrow_y, frame.heading_degrees, track.size);
  }
  const auto track_end = Clock::now();
  if (!separate) {
    DrawReadouts(canvas, frame, state.units);
    state.initialized = false;
    state.gauge_background.clear();
    state.gauge_text_background.clear();
  } else {
    if (!state.initialized) {
      canvas.Clear(0, 0, track.frame_x, height);
      DrawGForceBackground(canvas, cx, cy, radius);
      state.gauge_background =
          canvas.CopyRegion(0, gauge_top, left_width, height - gauge_top);
    }
    if (!state.initialized || state.lateral != frame.g_force.lateral_g ||
        state.longitudinal != frame.g_force.longitudinal_g ||
        state.magnitude != magnitude) {
      const bool text_below_dot = cy + radius + std::max(8, radius / 10) <
                                  magnitude_y - std::max(2, scale / 3);
      if (text_below_dot) {
        if (!state.initialized || state.magnitude != magnitude) {
          canvas.Blit(state.gauge_background, left_width, height - gauge_top, 0,
                      gauge_top);
          DrawMagnitude(canvas, magnitude, cx, magnitude_y, scale);
          state.gauge_text_background =
              canvas.CopyRegion(0, gauge_top, left_width, height - gauge_top,
                                std::move(state.gauge_text_background));
        }
        canvas.Blit(state.gauge_text_background, left_width, height - gauge_top,
                    0, gauge_top);
        DrawGForceDot(canvas, frame.g_force, cx, cy, radius);
      } else {
        canvas.Blit(state.gauge_background, left_width, height - gauge_top, 0,
                    gauge_top);
        DrawReadouts(canvas, frame, state.units, true, false, false);
      }
    }
    std::vector<int> speeds;
    for (SpeedUnit unit : state.units) {
      speeds.push_back(static_cast<int>(std::lround(std::clamp(
          frame.speed_meters_per_second *
              (unit == SpeedUnit::kMilesPerHour ? 2.2369362920544 : 3.6),
          0.0, 999.0))));
    }
    if (!state.initialized || speeds != state.speeds) {
      if (!state.units.empty()) canvas.Clear(0, 0, speed_right, speed_bottom);
      DrawReadouts(canvas, frame, state.units, false, true);
    }
    state.speeds = std::move(speeds);
    state.lateral = frame.g_force.lateral_g;
    state.longitudinal = frame.g_force.longitudinal_g;
    state.magnitude = magnitude;
    state.initialized = true;
  }
  state.pixels = canvas.TakePixels();
  state.previous_track = track;
  const auto end = Clock::now();
  state.timing = {std::chrono::duration<double>(buffer_end - start).count(),
                  std::chrono::duration<double>(track_end - buffer_end).count(),
                  std::chrono::duration<double>(end - track_end).count()};
  return absl::OkStatus();
}

absl::StatusOr<TrackRenderState> CreateTrackRenderState(
    const OverlayData& overlay, int frame_width, int frame_height) {
  if (overlay.track.empty()) {
    return absl::FailedPreconditionError("track data is empty");
  }
  if (frame_width < 160 || frame_height < 90 || frame_width > 7680 ||
      frame_height > 4320) {
    return absl::InvalidArgumentError("track frame dimensions are invalid");
  }
  const int margin = std::max(16, frame_height / 40);
  const int size =
      std::min(frame_width / 3, frame_height * 5 / 9) * 3 / 4;
  const int padding = size / 14;
  const int plot_size = size - padding * 2;
  const int track_thickness = std::max(5, size / 35);
  const int shadow_thickness = track_thickness + std::max(4, size / 50);

  TrackRenderState state{
      .frame_x = frame_width - size - margin,
      .frame_y = margin,
      .size = size,
      .blue_thickness = std::max(3, size / 70)};
  state.points.reserve(overlay.track.size());
  for (const TrackPoint& point : overlay.track) {
    state.points.push_back(
        {.x = padding + static_cast<int>(point.x * plot_size),
         .y = padding + static_cast<int>(point.y * plot_size)});
  }

  Canvas canvas(size, size);
  for (std::size_t i = 1; i < state.points.size(); ++i) {
    canvas.Line(state.points[i - 1].x, state.points[i - 1].y,
                state.points[i].x, state.points[i].y, shadow_thickness,
                kShadow);
  }
  for (std::size_t i = 1; i < state.points.size(); ++i) {
    canvas.Line(state.points[i - 1].x, state.points[i - 1].y,
                state.points[i].x, state.points[i].y, track_thickness,
                kWhite);
  }
  state.pixels = canvas.TakePixels();
  return state;
}

absl::StatusOr<TrackFrameSnapshot> AdvanceTrackRenderState(
    std::size_t explored_point_count, TrackRenderState* state,
    bool reuse_snapshot) {
  const std::size_t target =
      std::min(explored_point_count, state->points.size());
  if (target < state->explored_point_count) {
    return absl::InvalidArgumentError(
        "track render timestamps must be processed in increasing order");
  }
  const bool changed =
      target > std::max<std::size_t>(1, state->explored_point_count);
  Canvas canvas(state->size, state->size, std::move(state->pixels));
  for (std::size_t i = std::max<std::size_t>(1, state->explored_point_count);
       i < target; ++i) {
    canvas.Line(state->points[i - 1].x, state->points[i - 1].y,
                state->points[i].x, state->points[i].y,
                state->blue_thickness, kBlue);
  }
  state->explored_point_count = target;
  state->pixels = canvas.TakePixels();
  if (changed || !state->snapshot || !reuse_snapshot) {
    state->snapshot =
        std::make_shared<const std::vector<std::uint8_t>>(state->pixels);
  }

  TrackFrameSnapshot snapshot{.frame_x = state->frame_x,
                              .frame_y = state->frame_y,
                              .size = state->size,
                              .arrow_x = 0,
                              .arrow_y = 0,
                              .has_arrow = target > 0,
                              .pixels = state->snapshot};
  if (target > 0) {
    snapshot.arrow_x = state->points[target - 1].x;
    snapshot.arrow_y = state->points[target - 1].y;
  }
  return snapshot;
}

absl::StatusOr<std::vector<std::uint8_t>> RenderOverlayFrameRgba(
    const OverlayFrameData& frame, const TrackFrameSnapshot& track, int width,
    int height, const std::vector<SpeedUnit>& speed_units) {
  const std::size_t expected_track_bytes =
      static_cast<std::size_t>(track.size) * track.size * 4;
  if (width < 160 || height < 90 || width > 7680 || height > 4320 ||
      track.size <= 0 || track.frame_x < 0 || track.frame_y < 0 ||
      track.frame_x + track.size > width ||
      track.frame_y + track.size > height || !track.pixels ||
      track.pixels->size() != expected_track_bytes) {
    return absl::InvalidArgumentError(
        "overlay frame or track snapshot dimensions are invalid");
  }
  Canvas canvas(width, height);
  canvas.Blit(*track.pixels, track.size, track.size, track.frame_x,
              track.frame_y);
  if (track.has_arrow) {
    DrawArrow(canvas, track.frame_x + track.arrow_x,
              track.frame_y + track.arrow_y, frame.heading_degrees,
              track.size);
  }
  DrawReadouts(canvas, frame, speed_units);
  return canvas.TakePixels();
}

absl::StatusOr<std::vector<std::uint8_t>> RenderOverlayFrameRgba(
    const TelemetryData& telemetry, const OverlayData& overlay,
    double timestamp_seconds, int width, int height,
    const std::vector<SpeedUnit>& speed_units) {
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
  const int track_size =
      std::min(width / 3, height * 5 / 9) * 3 / 4;
  DrawTrack(canvas, overlay, frame->explored_track_point_count,
            frame->heading_degrees,
            width - track_size - margin, margin, track_size);
  DrawReadouts(canvas, *frame, speed_units);
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
  absl::StatusOr<TrackRenderState> track =
      CreateTrackRenderState(overlay, options.width, options.height);
  if (!track.ok()) return track.status();
  CachedOverlayRenderer renderer(options.width, options.height,
                                 options.speed_units);
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
    absl::StatusOr<OverlayFrameData> frame = SampleOverlayFrame(
        telemetry, overlay, absl::Seconds(seconds));
    if (!frame.ok()) return frame.status();
    absl::StatusOr<TrackFrameSnapshot> track_snapshot =
        AdvanceTrackRenderState(frame->explored_track_point_count, &*track);
    if (!track_snapshot.ok()) return track_snapshot.status();
    const absl::Status rendered = renderer.Render(*frame, *track_snapshot);
    if (!rendered.ok()) return rendered;
    const absl::Status status =
        WritePng(path, options.width, options.height, renderer.pixels());
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

}  // namespace racevideo
