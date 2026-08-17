#include "parser/mp4_gpmf.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace racevideo {
namespace {

struct Box {
  std::uint64_t data;
  std::uint64_t end;
  std::array<char, 4> type;
};
struct ChunkRule {
  std::uint32_t first;
  std::uint32_t count;
  std::uint32_t description;
};
struct TimeRule {
  std::uint32_t count;
  std::uint32_t duration;
};
struct Tables {
  std::uint32_t gpmd_description = 0;
  std::uint32_t timescale = 0;
  std::vector<std::uint32_t> sizes;
  std::vector<std::uint64_t> chunks;
  std::vector<ChunkRule> chunk_rules;
  std::vector<TimeRule> time_rules;
};

absl::Status Bad(std::string_view message) {
  return absl::DataLossError(absl::StrCat("invalid MP4: ", message));
}
absl::Status Seek(std::istream& in, std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    return absl::OutOfRangeError("MP4 offset exceeds stream limits");
  }
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  return in ? absl::OkStatus() : Bad("cannot seek");
}
absl::StatusOr<std::uint32_t> U32(std::istream& in) {
  std::array<unsigned char, 4> b{};
  if (!in.read(reinterpret_cast<char*>(b.data()), 4)) return Bad("truncated");
  return (static_cast<std::uint32_t>(b[0]) << 24) |
         (static_cast<std::uint32_t>(b[1]) << 16) |
         (static_cast<std::uint32_t>(b[2]) << 8) | b[3];
}
absl::StatusOr<std::uint64_t> U64(std::istream& in) {
  absl::StatusOr<std::uint32_t> hi = U32(in);
  if (!hi.ok()) return hi.status();
  absl::StatusOr<std::uint32_t> lo = U32(in);
  if (!lo.ok()) return lo.status();
  return (static_cast<std::uint64_t>(*hi) << 32) | *lo;
}
bool Type(const Box& box, std::string_view type) {
  return std::string_view(box.type.data(), 4) == type;
}
bool Container(const Box& box) {
  return Type(box, "moov") || Type(box, "mdia") || Type(box, "minf") ||
         Type(box, "stbl");
}
absl::StatusOr<Box> ReadBox(std::istream& in, std::uint64_t offset,
                            std::uint64_t parent_end) {
  absl::Status status = Seek(in, offset);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> size32 = U32(in);
  if (!size32.ok()) return size32.status();
  std::array<char, 4> type{};
  if (!in.read(type.data(), 4)) return Bad("truncated box type");
  std::uint64_t header = 8;
  std::uint64_t size = *size32;
  if (size == 1) {
    absl::StatusOr<std::uint64_t> large = U64(in);
    if (!large.ok()) return large.status();
    size = *large;
    header = 16;
  } else if (size == 0) {
    size = parent_end - offset;
  }
  if (size < header || size > parent_end - offset) return Bad("box size");
  return Box{.data = offset + header, .end = offset + size, .type = type};
}
absl::Status Position(std::istream& in, const Box& box, std::uint64_t skip,
                      std::uint64_t bytes) {
  if (skip > box.end - box.data || bytes > box.end - box.data - skip) {
    return Bad("short box payload");
  }
  return Seek(in, box.data + skip);
}

absl::Status Stsd(std::istream& in, const Box& box, Tables& t) {
  absl::Status status = Position(in, box, 4, 4);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> count = U32(in);
  if (!count.ok()) return count.status();
  std::uint64_t offset = box.data + 8;
  for (std::uint32_t i = 1; i <= *count; ++i) {
    absl::StatusOr<Box> entry = ReadBox(in, offset, box.end);
    if (!entry.ok()) return entry.status();
    if (Type(*entry, "gpmd")) t.gpmd_description = i;
    offset = entry->end;
  }
  return absl::OkStatus();
}
absl::Status Mdhd(std::istream& in, const Box& box, Tables& t) {
  absl::Status status = Position(in, box, 0, 4);
  if (!status.ok()) return status;
  std::array<unsigned char, 4> header{};
  if (!in.read(reinterpret_cast<char*>(header.data()), 4)) return Bad("mdhd");
  status = Position(in, box, header[0] == 1 ? 20 : 12, 4);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> value = U32(in);
  if (!value.ok()) return value.status();
  t.timescale = *value;
  return absl::OkStatus();
}
absl::Status Stsz(std::istream& in, const Box& box, Tables& t) {
  absl::Status status = Position(in, box, 4, 8);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> fixed = U32(in);
  absl::StatusOr<std::uint32_t> count = U32(in);
  if (!fixed.ok()) return fixed.status();
  if (!count.ok()) return count.status();
  if (*fixed == 0 && *count > (box.end - box.data - 12) / 4) {
    return Bad("stsz count");
  }
  t.sizes.assign(*count, *fixed);
  if (*fixed == 0) {
    for (std::uint32_t& size : t.sizes) {
      absl::StatusOr<std::uint32_t> value = U32(in);
      if (!value.ok()) return value.status();
      size = *value;
    }
  }
  return absl::OkStatus();
}
absl::Status ChunkOffsets(std::istream& in, const Box& box, bool wide,
                          Tables& t) {
  absl::Status status = Position(in, box, 4, 4);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> count = U32(in);
  if (!count.ok()) return count.status();
  const std::uint64_t width = wide ? 8 : 4;
  if (*count > (box.end - box.data - 8) / width) return Bad("chunk count");
  for (std::uint32_t i = 0; i < *count; ++i) {
    if (wide) {
      absl::StatusOr<std::uint64_t> value = U64(in);
      if (!value.ok()) return value.status();
      t.chunks.push_back(*value);
    } else {
      absl::StatusOr<std::uint32_t> value = U32(in);
      if (!value.ok()) return value.status();
      t.chunks.push_back(*value);
    }
  }
  return absl::OkStatus();
}
absl::Status Stsc(std::istream& in, const Box& box, Tables& t) {
  absl::Status status = Position(in, box, 4, 4);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> count = U32(in);
  if (!count.ok()) return count.status();
  if (*count > (box.end - box.data - 8) / 12) return Bad("stsc count");
  for (std::uint32_t i = 0; i < *count; ++i) {
    absl::StatusOr<std::uint32_t> first = U32(in);
    absl::StatusOr<std::uint32_t> samples = U32(in);
    absl::StatusOr<std::uint32_t> description = U32(in);
    if (!first.ok()) return first.status();
    if (!samples.ok()) return samples.status();
    if (!description.ok()) return description.status();
    t.chunk_rules.push_back({*first, *samples, *description});
  }
  return absl::OkStatus();
}
absl::Status Stts(std::istream& in, const Box& box, Tables& t) {
  absl::Status status = Position(in, box, 4, 4);
  if (!status.ok()) return status;
  absl::StatusOr<std::uint32_t> count = U32(in);
  if (!count.ok()) return count.status();
  if (*count > (box.end - box.data - 8) / 8) return Bad("stts count");
  for (std::uint32_t i = 0; i < *count; ++i) {
    absl::StatusOr<std::uint32_t> samples = U32(in);
    absl::StatusOr<std::uint32_t> duration = U32(in);
    if (!samples.ok()) return samples.status();
    if (!duration.ok()) return duration.status();
    t.time_rules.push_back({*samples, *duration});
  }
  return absl::OkStatus();
}

absl::Status ParseTrack(std::istream& in, std::uint64_t begin,
                        std::uint64_t end, int depth, Tables& t) {
  if (depth > 16) return Bad("box nesting");
  for (std::uint64_t offset = begin; offset < end;) {
    if (end - offset < 8) return Bad("box header");
    absl::StatusOr<Box> box = ReadBox(in, offset, end);
    if (!box.ok()) return box.status();
    absl::Status status;
    if (Type(*box, "stsd")) status = Stsd(in, *box, t);
    else if (Type(*box, "mdhd")) status = Mdhd(in, *box, t);
    else if (Type(*box, "stsz")) status = Stsz(in, *box, t);
    else if (Type(*box, "stco")) status = ChunkOffsets(in, *box, false, t);
    else if (Type(*box, "co64")) status = ChunkOffsets(in, *box, true, t);
    else if (Type(*box, "stsc")) status = Stsc(in, *box, t);
    else if (Type(*box, "stts")) status = Stts(in, *box, t);
    else if (Container(*box))
      status = ParseTrack(in, box->data, box->end, depth + 1, t);
    if (!status.ok()) return status;
    offset = box->end;
  }
  return absl::OkStatus();
}

absl::StatusOr<GpmfTrackInfo> Build(const Tables& t,
                                    std::uint64_t file_size) {
  if (t.gpmd_description == 0 || t.timescale == 0 || t.sizes.empty() ||
      t.chunks.empty() || t.chunk_rules.empty() || t.time_rules.empty()) {
    return Bad("incomplete GPMF sample tables");
  }
  GpmfTrackInfo out{.file_size_bytes = file_size, .timescale = t.timescale};
  out.payloads.reserve(t.sizes.size());
  std::size_t sample = 0;
  std::size_t rule = 0;
  for (std::size_t chunk = 0; chunk < t.chunks.size(); ++chunk) {
    const std::uint32_t number = static_cast<std::uint32_t>(chunk + 1);
    while (rule + 1 < t.chunk_rules.size() &&
           t.chunk_rules[rule + 1].first <= number) ++rule;
    if (t.chunk_rules[rule].first > number) return Bad("stsc first chunk");
    std::uint64_t offset = t.chunks[chunk];
    for (std::uint32_t i = 0; i < t.chunk_rules[rule].count; ++i) {
      if (sample >= t.sizes.size()) return Bad("too many chunk samples");
      const std::uint32_t size = t.sizes[sample++];
      if (offset > file_size || size > file_size - offset) {
        return Bad("sample outside file");
      }
      out.payloads.push_back({.file_offset = offset, .size_bytes = size});
      offset += size;
    }
  }
  if (sample != t.sizes.size()) return Bad("too few chunk samples");
  std::size_t timed = 0;
  std::uint64_t start = 0;
  for (const TimeRule& r : t.time_rules) {
    for (std::uint32_t i = 0; i < r.count; ++i) {
      if (timed >= out.payloads.size()) return Bad("too many timed samples");
      out.payloads[timed].start_time_units = start;
      out.payloads[timed].duration_units = r.duration;
      start += r.duration;
      ++timed;
    }
  }
  if (timed != out.payloads.size()) return Bad("too few timed samples");
  return out;
}

absl::StatusOr<std::optional<GpmfTrackInfo>> Search(
    std::istream& in, std::uint64_t begin, std::uint64_t end, int depth,
    std::uint64_t file_size) {
  if (depth > 16) return Bad("box nesting");
  for (std::uint64_t offset = begin; offset < end;) {
    if (end - offset < 8) return Bad("box header");
    absl::StatusOr<Box> box = ReadBox(in, offset, end);
    if (!box.ok()) return box.status();
    if (Type(*box, "trak")) {
      Tables tables;
      absl::Status status = ParseTrack(in, box->data, box->end, depth + 1, tables);
      if (!status.ok()) return status;
      if (tables.gpmd_description != 0) {
        absl::StatusOr<GpmfTrackInfo> result = Build(tables, file_size);
        if (!result.ok()) return result.status();
        return std::optional<GpmfTrackInfo>(std::move(*result));
      }
    } else if (Container(*box)) {
      absl::StatusOr<std::optional<GpmfTrackInfo>> found =
          Search(in, box->data, box->end, depth + 1, file_size);
      if (!found.ok() || found->has_value()) return found;
    }
    offset = box->end;
  }
  return std::nullopt;
}

}  // namespace

absl::StatusOr<GpmfTrackInfo> IndexGpmfTrack(
    const std::filesystem::path& input_path) {
  std::error_code error;
  const std::uint64_t size = std::filesystem::file_size(input_path, error);
  if (error) return absl::NotFoundError(error.message());
  std::ifstream input(input_path, std::ios::binary);
  if (!input) return absl::PermissionDeniedError("cannot open input file");
  absl::StatusOr<std::optional<GpmfTrackInfo>> found =
      Search(input, 0, size, 0, size);
  if (!found.ok()) return found.status();
  if (!found->has_value()) return absl::NotFoundError("no GPMF track");
  return std::move(**found);
}

}  // namespace racevideo
