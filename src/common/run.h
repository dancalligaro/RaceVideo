#ifndef RACEVIDEO_COMMON_RUN_H_
#define RACEVIDEO_COMMON_RUN_H_

#include "absl/status/status.h"
#include "cli/options.h"

namespace racevideo {

absl::Status Run(const Options& options);

}  // namespace racevideo

#endif  // RACEVIDEO_COMMON_RUN_H_

