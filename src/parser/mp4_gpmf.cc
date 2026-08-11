#include "parser/mp4_gpmf.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <string_view>
#include <system_error>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace racevideo {
namespace {

constexpr int kMaximumBoxDepth = 16;
constexpr std::uint64_t kBoxHeaderSize = 8;

struct Box {
  std::uint64_t data_offset;
  std::uint64_t end_offset;
  std::array<char, 4> type;
};

absl::Status ReadFailure(std::string_view description) {
  return absl::DataLossError(absl::StrCat("truncated MP4 ", description));
}

absl::StatusOr<std::uint32_t> ReadUint32(std::istream& input) {
  std::array<unsigned char, 4> bytes{};
  if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
    return ReadFailure("integer");
  }
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) |
         static_cast<std::uint32_t>(bytes[3]);
}

absl::StatusOr<std::uint64_t> ReadUint64(std::istream& input) {
  absl::StatusOr<std::uint32_t> high = ReadUint32(input);
  if (!high.ok()) return high.status();
  absl::StatusOr<std::uint32_t> low = ReadUint32(input);
  if (!low.ok()) return low.status();
  return (static_cast<std::uint64_t>(*high) << 32) | *low;
}

bool IsType(const std::array<char, 4>& type, std::string_view expected) {
  return std::string_view(type.data(), type.size()) == expected;
}

bool IsContainer(const std::array<char, 4>& type) {
  return IsType(type, "moov") || IsType(type, "trak") ||
         IsType(type, "mdia") || IsType(type, "minf") ||
         IsType(type, "stbl");
}

absl::Status Seek(std::istream& input, std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    return absl::OutOfRangeError("MP4 offset is too large");
  }
  input.clear();
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) return ReadFailure("offset");
  return absl::OkStatus();
}

absl::StatusOr<Box> ReadBox(std::istream& input, std::uint64_t offset,
                            std::uint64_t parent_end) {
  absl::Status status = Seek(input, offset);
  if (!status.ok()) return status;

  absl::StatusOr<std::uint32_t> size32 = ReadUint32(input);
  if (!size32.ok()) return size32.status();

  std::array<char, 4> type{};
  if (!input.read(type.data(), type.size())) return ReadFailure("box type");

  std::uint64_t header_size = kBoxHeaderSize;
  std::uint64_t box_size = *size32;
  if (box_size == 1) {
    absl::StatusOr<std::uint64_t> extended_size = ReadUint64(input);
    if (!extended_size.ok()) return extended_size.status();
    box_size = *extended_size;
    header_size = 16;
  } else if (box_size == 0) {
    box_size = parent_end - offset;
  }

  if (box_size < header_size || box_size > parent_end - offset) {
    return absl::DataLossError("invalid MP4 box size");
  }
  return Box{.data_offset = offset + header_size,
             .end_offset = offset + box_size,
             .type = type};
}

absl::StatusOr<bool> ScanSampleDescriptions(std::istream& input,
                                             const Box& stsd) {
  if (stsd.end_offset - stsd.data_offset < 8) {
    return ReadFailure("sample description");
  }
  absl::Status status = Seek(input, stsd.data_offset + 4);
  if (!status.ok()) return status;

  absl::StatusOr<std::uint32_t> entry_count = ReadUint32(input);
  if (!entry_count.ok()) return entry_count.status();

  std::uint64_t offset = stsd.data_offset + 8;
  for (std::uint32_t index = 0; index < *entry_count; ++index) {
    absl::StatusOr<Box> entry = ReadBox(input, offset, stsd.end_offset);
    if (!entry.ok()) return entry.status();
    if (IsType(entry->type, "gpmd")) return true;
    offset = entry->end_offset;
  }
  return false;
}

absl::StatusOr<bool> ScanBoxes(std::istream& input, std::uint64_t begin,
                               std::uint64_t end, int depth) {
  if (depth > kMaximumBoxDepth) {
    return absl::ResourceExhaustedError("MP4 box nesting is too deep");
  }

  std::uint64_t offset = begin;
  while (offset < end) {
    if (end - offset < kBoxHeaderSize) return ReadFailure("box header");
    absl::StatusOr<Box> box = ReadBox(input, offset, end);
    if (!box.ok()) return box.status();

    if (IsType(box->type, "stsd")) {
      absl::StatusOr<bool> found = ScanSampleDescriptions(input, *box);
      if (!found.ok() || *found) return found;
    } else if (IsContainer(box->type)) {
      absl::StatusOr<bool> found =
          ScanBoxes(input, box->data_offset, box->end_offset, depth + 1);
      if (!found.ok() || *found) return found;
    }
    offset = box->end_offset;
  }
  return false;
}

}  // namespace

absl::StatusOr<GpmfTrackInfo> FindGpmfTrack(
    const std::filesystem::path& input_path) {
  std::error_code error;
  const std::uint64_t file_size = std::filesystem::file_size(input_path, error);
  if (error) {
    return absl::NotFoundError(absl::StrCat(
        "cannot inspect input file: ", input_path.string(), ": ",
        error.message()));
  }

  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    return absl::PermissionDeniedError(
        absl::StrCat("cannot open input file: ", input_path.string()));
  }

  absl::StatusOr<bool> found = ScanBoxes(input, 0, file_size, 0);
  if (!found.ok()) return found.status();
  if (!*found) {
    return absl::NotFoundError("input does not contain a GPMF metadata track");
  }
  return GpmfTrackInfo{.file_size_bytes = file_size};
}

}  // namespace racevideo

