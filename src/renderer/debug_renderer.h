#ifndef RACEVIDEO_RENDERER_DEBUG_RENDERER_H_
#define RACEVIDEO_RENDERER_DEBUG_RENDERER_H_

#include <filesystem>

#include "absl/status/status.h"
#include "overlay/overlay_data.h"
#include "telemetry/telemetry.h"

namespace racevideo {

struct DebugRenderOptions {
  std::filesystem::path output_directory;
  double start_seconds;
  double duration_seconds;
  double frames_per_second;
  int width;
  int height;
};

absl::Status RenderDebugFrames(const TelemetryData& telemetry,
                               const OverlayData& overlay,
                               const DebugRenderOptions& options);

}  // namespace racevideo

#endif  // RACEVIDEO_RENDERER_DEBUG_RENDERER_H_
