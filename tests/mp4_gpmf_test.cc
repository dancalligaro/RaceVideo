#include "parser/mp4_gpmf.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace racevideo {
namespace {

void AppendUint32(std::vector<char>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<char>(value >> 24));
  bytes.push_back(static_cast<char>(value >> 16));
  bytes.push_back(static_cast<char>(value >> 8));
  bytes.push_back(static_cast<char>(value));
}

std::vector<char> Box(std::string_view type, const std::vector<char>& payload) {
  std::vector<char> bytes;
  AppendUint32(bytes, static_cast<std::uint32_t>(8 + payload.size()));
  bytes.insert(bytes.end(), type.begin(), type.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

std::filesystem::path WriteTestFile(std::string_view name,
                                    const std::vector<char>& bytes) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / name;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return path;
}

TEST(FindGpmfTrackTest, FindsGpmdSampleDescription) {
  std::vector<char> stsd_payload(4, 0);
  AppendUint32(stsd_payload, 1);
  const std::vector<char> gpmd = Box("gpmd", {});
  stsd_payload.insert(stsd_payload.end(), gpmd.begin(), gpmd.end());

  const std::vector<char> stsd = Box("stsd", stsd_payload);
  const std::vector<char> stbl = Box("stbl", stsd);
  const std::vector<char> minf = Box("minf", stbl);
  const std::vector<char> mdia = Box("mdia", minf);
  const std::vector<char> trak = Box("trak", mdia);
  const std::vector<char> moov = Box("moov", trak);
  const std::filesystem::path path =
      WriteTestFile("racevideo_gpmf_test.mp4", moov);

  const absl::StatusOr<GpmfTrackInfo> track = FindGpmfTrack(path);

  ASSERT_TRUE(track.ok()) << track.status();
  EXPECT_EQ(track->file_size_bytes, moov.size());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(FindGpmfTrackTest, RejectsFileWithoutGpmdDescription) {
  const std::vector<char> ftyp = Box("ftyp", {});
  const std::filesystem::path path =
      WriteTestFile("racevideo_no_gpmf_test.mp4", ftyp);

  const absl::StatusOr<GpmfTrackInfo> track = FindGpmfTrack(path);

  EXPECT_EQ(track.status().code(), absl::StatusCode::kNotFound);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(FindGpmfTrackTest, RejectsTruncatedBox) {
  const std::array<char, 8> bytes = {0, 0, 0, 16, 'm', 'o', 'o', 'v'};
  const std::filesystem::path path = WriteTestFile(
      "racevideo_truncated_test.mp4", {bytes.begin(), bytes.end()});

  const absl::StatusOr<GpmfTrackInfo> track = FindGpmfTrack(path);

  EXPECT_EQ(track.status().code(), absl::StatusCode::kDataLoss);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

}  // namespace
}  // namespace racevideo

