#include "cli/options.h"

#include <string>
#include <vector>

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

TEST(ParseSpeedUnitsTest, SupportsHiddenAndEveryDocumentedOrdering) {
  EXPECT_EQ(*ParseSpeedUnits(""), std::vector<SpeedUnit>({}));
  EXPECT_EQ(*ParseSpeedUnits("kmh"),
            std::vector<SpeedUnit>({SpeedUnit::kKilometersPerHour}));
  EXPECT_EQ(*ParseSpeedUnits("mph"),
            std::vector<SpeedUnit>({SpeedUnit::kMilesPerHour}));
  EXPECT_EQ(*ParseSpeedUnits("kmh,mph"),
            std::vector<SpeedUnit>({SpeedUnit::kKilometersPerHour,
                                    SpeedUnit::kMilesPerHour}));
  EXPECT_EQ(*ParseSpeedUnits("mph,kmh"),
            std::vector<SpeedUnit>({SpeedUnit::kMilesPerHour,
                                    SpeedUnit::kKilometersPerHour}));
}

TEST(ParseSpeedUnitsTest, IsCaseInsensitiveAndRejectsOtherLists) {
  EXPECT_EQ(*ParseSpeedUnits("MPH,KMH"),
            std::vector<SpeedUnit>({SpeedUnit::kMilesPerHour,
                                    SpeedUnit::kKilometersPerHour}));
  EXPECT_EQ(ParseSpeedUnits("kmh,kmh").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseSpeedUnits("mph,").status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ParseVideoEncoderTest, SupportsSoftwareAndNvidia) {
  EXPECT_EQ(*ParseVideoEncoder("software"), VideoEncoder::kSoftware);
  EXPECT_EQ(*ParseVideoEncoder("NVIDIA"), VideoEncoder::kNvidia);
  EXPECT_EQ(ParseVideoEncoder("automatic").status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace racevideo
