#include "session_commands.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace racevideo {
namespace {

using Defaults = std::vector<std::pair<std::string, std::string>>;

std::string Trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  return std::string(value.substr(first, value.find_last_not_of(" \t\r\n") -
                                            first + 1));
}

absl::StatusOr<Defaults> ReadDefaults(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) return absl::NotFoundError("cannot open defaults file");
  Defaults defaults;
  std::map<std::string, bool> seen;
  std::string line;
  int number = 0;
  bool has_axes = false;
  while (std::getline(input, line)) {
    ++number;
    // Allow UTF-8 BOMs, including defaults written by Windows editors.
    if (number == 1 && line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
    line = Trim(line);
    if (line.empty() || line.starts_with('#')) continue;
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      return absl::InvalidArgumentError(
          absl::StrCat("defaults line ", number, " must be flag=value"));
    }
    std::string key = Trim(std::string_view(line).substr(0, equals));
    if (key.starts_with("--")) key.erase(0, 2);
    if (key.empty() || !std::all_of(key.begin(), key.end(), [](unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) {
      return absl::InvalidArgumentError(
          absl::StrCat("invalid flag name on defaults line ", number));
    }
    if (key == "input" || key == "input_list" || key == "output_video") {
      return absl::InvalidArgumentError(
          absl::StrCat("--", key, " is generated; remove it from defaults"));
    }
    if (!seen.emplace(key, true).second) {
      return absl::InvalidArgumentError(absl::StrCat("duplicate default: ", key));
    }
    std::string value = Trim(std::string_view(line).substr(equals + 1));
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }
    if (value.find('\0') != std::string::npos) {
      return absl::InvalidArgumentError("defaults contain a null byte");
    }
    if (key == "imu_axis_order" && !value.empty()) has_axes = true;
    defaults.emplace_back(std::move(key), std::move(value));
  }
  if (!input.eof()) return absl::DataLossError("cannot read defaults file");
  if (!has_axes) {
    return absl::InvalidArgumentError(
        "defaults must supply imu_axis_order for video rendering");
  }
  return defaults;
}

struct Chapter {
  std::string session;
  int index;
  int first_index;
};

bool ParseChapter(std::string filename, Chapter* chapter) {
  std::transform(filename.begin(), filename.end(), filename.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  if (filename.size() != 12 || filename.substr(8) != ".MP4" ||
      !std::all_of(filename.begin() + 4, filename.begin() + 8,
                   [](char c) { return c >= '0' && c <= '9'; })) return false;
  const std::string id = filename.substr(4, 4);
  if (filename.starts_with("GOPR")) {
    *chapter = {.session = "GOPR" + id, .index = 0, .first_index = 0};
    return true;
  }
  const std::string family = filename.substr(0, 2);
  if (family != "GP" && family != "GH" && family != "GX") return false;
  if (filename[2] < '0' || filename[2] > '9' ||
      filename[3] < '0' || filename[3] > '9') return false;
  const int index = (filename[2] - '0') * 10 + filename[3] - '0';
  if (index == 0) return false;
  *chapter = {.session = family == "GP" ? "GOPR" + id : family + "01" + id,
              .index = index,
              .first_index = family == "GP" ? 0 : 1};
  return true;
}

std::string Quote(std::string_view value, bool powershell) {
  std::string result = "'";
  for (const char c : value) {
    if (c == '\'') result += powershell ? "''" : "'\\''";
    else result += c;
  }
  return result + "'";
}

}  // namespace

absl::StatusOr<std::string> GenerateSessionCommands(
    const SessionCommandOptions& options) {
  auto defaults = ReadDefaults(options.defaults);
  if (!defaults.ok()) return defaults.status();
  std::error_code error;
  const auto folder = std::filesystem::absolute(options.folder, error)
                          .lexically_normal();
  if (error) return absl::InvalidArgumentError("cannot resolve input folder");
  std::filesystem::directory_iterator entry(folder, error);
  if (error) return absl::NotFoundError("cannot scan input folder");
  using Chapters = std::map<int, std::filesystem::path>;
  std::map<std::string, std::pair<int, Chapters>> sessions;
  for (const auto end = std::filesystem::directory_iterator(); entry != end;
       entry.increment(error)) {
    if (error) return absl::DataLossError("cannot read input folder");
    const bool regular = entry->is_regular_file(error);
    if (error) return absl::DataLossError("cannot inspect input file");
    if (!regular) continue;
    Chapter chapter;
    if (!ParseChapter(entry->path().filename().string(), &chapter)) continue;
    auto& [first, chapters] = sessions[chapter.session];
    first = chapter.first_index;
    if (!chapters.emplace(chapter.index, entry->path()).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("duplicate chapter in session ", chapter.session));
    }
  }
  if (error) return absl::DataLossError("cannot read input folder");
  if (sessions.empty()) return absl::NotFoundError("no GoPro chapters found");
  const std::string executable = options.executable.empty()
                                     ? (options.powershell ? "racevideo.exe"
                                                           : "racevideo")
                                     : options.executable;
  std::string script = options.powershell ? "" : "#!/usr/bin/env bash\nset -e\n\n";
  const std::string continuation = options.powershell ? " `\n" : " \\\n";
  for (const auto& [session, contents] : sessions) {
    const auto& [first, chapters] = contents;
    int expected = first;
    std::vector<std::string> arguments;
    for (const auto& [index, path] : chapters) {
      if (index != expected++) {
        return absl::InvalidArgumentError(
            absl::StrCat("missing chapter ", expected - 1, " in session ", session));
      }
      arguments.push_back("--input=" + path.string());
    }
    arguments.push_back("--output_video=" + options.output_prefix + session + ".mp4");
    for (const auto& [key, value] : *defaults) {
      arguments.push_back("--" + key + "=" + value);
    }
    script += (options.powershell ? "& " : "") + Quote(executable, options.powershell);
    for (const auto& argument : arguments) {
      script += continuation + "  " + Quote(argument, options.powershell);
    }
    script += '\n';
    if (options.powershell) script += "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\n";
    script += '\n';
  }
  return script;
}

}  // namespace racevideo
