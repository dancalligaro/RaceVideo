#include "cli/options.h"

#include <string>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(ParseOptionsTest, RequiresInput) {
  char program[] = "racevideo";
  char* argv[] = {program};

  const absl::StatusOr<Options> options = ParseOptions(1, argv);

  EXPECT_EQ(options.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace racevideo

