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

TEST(IndexGpmfTrackTest, MapsSampleOffsetsAndTimes) {
  std::vector<char> stsd_payload(4, 0);
  AppendUint32(stsd_payload, 1);
  const std::vector<char> gpmd = Box("gpmd", {});
  stsd_payload.insert(stsd_payload.end(), gpmd.begin(), gpmd.end());

  std::vector<char> stsz_payload(4, 0);
  AppendUint32(stsz_payload, 0);
  AppendUint32(stsz_payload, 2);
  AppendUint32(stsz_payload, 4);
  AppendUint32(stsz_payload, 6);

  std::vector<char> stco_payload(4, 0);
  AppendUint32(stco_payload, 1);
  AppendUint32(stco_payload, 0);

  std::vector<char> stsc_payload(4, 0);
  AppendUint32(stsc_payload, 1);
  AppendUint32(stsc_payload, 1);
  AppendUint32(stsc_payload, 2);
  AppendUint32(stsc_payload, 1);

  std::vector<char> stts_payload(4, 0);
  AppendUint32(stts_payload, 1);
  AppendUint32(stts_payload, 2);
  AppendUint32(stts_payload, 1001);

  std::vector<char> stbl_payload;
  for (const std::vector<char>& child :
       {Box("stsd", stsd_payload), Box("stsz", stsz_payload),
        Box("stco", stco_payload), Box("stsc", stsc_payload),
        Box("stts", stts_payload)}) {
    stbl_payload.insert(stbl_payload.end(), child.begin(), child.end());
  }
  const std::vector<char> stbl = Box("stbl", stbl_payload);
  const std::vector<char> minf = Box("minf", stbl);

  std::vector<char> mdhd_payload(12, 0);
  AppendUint32(mdhd_payload, 1000);
  const std::vector<char> mdhd = Box("mdhd", mdhd_payload);
  std::vector<char> mdia_payload = mdhd;
  mdia_payload.insert(mdia_payload.end(), minf.begin(), minf.end());
  const std::vector<char> mdia = Box("mdia", mdia_payload);
  const std::vector<char> trak = Box("trak", mdia);
  const std::vector<char> moov = Box("moov", trak);
  const std::filesystem::path path =
      WriteTestFile("racevideo_index_test.mp4", moov);

  const absl::StatusOr<GpmfTrackInfo> track = IndexGpmfTrack(path);

  ASSERT_TRUE(track.ok()) << track.status();
  ASSERT_EQ(track->payloads.size(), 2);
  EXPECT_EQ(track->timescale, 1000);
  EXPECT_EQ(track->payloads[0].file_offset, 0);
  EXPECT_EQ(track->payloads[0].size_bytes, 4);
  EXPECT_EQ(track->payloads[1].file_offset, 4);
  EXPECT_EQ(track->payloads[1].start_time_units, 1001);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

}  // namespace
}  // namespace racevideo
