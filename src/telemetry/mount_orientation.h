#ifndef RACEVIDEO_TELEMETRY_MOUNT_ORIENTATION_H_
#define RACEVIDEO_TELEMETRY_MOUNT_ORIENTATION_H_

#include <string_view>

#include "absl/status/status.h"
#include "telemetry/telemetry.h"

namespace racevideo {

absl::Status DetectAndApplyMountOrientation(TelemetryData* telemetry);
std::string_view MountOrientationName(MountOrientation orientation);

}  // namespace racevideo

#endif  // RACEVIDEO_TELEMETRY_MOUNT_ORIENTATION_H_
