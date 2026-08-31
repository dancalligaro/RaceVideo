#ifndef RACEVIDEO_TELEMETRY_AXIS_MAPPING_H_
#define RACEVIDEO_TELEMETRY_AXIS_MAPPING_H_

#include <string_view>

#include "absl/status/status.h"
#include "telemetry/telemetry.h"

namespace racevideo {

// Converts encoded components to camera X,Y,Z. Each order character identifies
// the axis represented by that input component; lowercase reverses its sign.
absl::Status NormalizeInertialAxes(std::string_view source_axis_order,
                                   TelemetryData* telemetry);

}  // namespace racevideo

#endif  // RACEVIDEO_TELEMETRY_AXIS_MAPPING_H_
