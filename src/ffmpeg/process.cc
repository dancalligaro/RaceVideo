#include "ffmpeg/process.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace racevideo {
namespace {

constexpr std::size_t kMaximumCapturedOutputBytes = 1024 * 1024;

#ifdef _WIN32
class Handle {
 public:
  Handle() = default;
  explicit Handle(HANDLE handle) : handle_(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }
  HANDLE get() const { return handle_; }
  HANDLE release() {
    HANDLE result = handle_;
    handle_ = nullptr;
    return result;
  }

 private:
  HANDLE handle_ = nullptr;
};

std::wstring Widen(std::string_view value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.data(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), length);
  return result;
}

std::wstring QuoteArgument(std::wstring_view argument) {
  if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
    return std::wstring(argument);
  }
  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;
  for (wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
    } else if (character == L'\"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(character);
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      backslashes = 0;
      quoted.push_back(character);
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

std::wstring BuildCommandLine(const std::filesystem::path& executable,
                              const std::vector<std::string>& arguments) {
  std::wstring command_line = QuoteArgument(executable.native());
  for (const std::string& argument : arguments) {
    command_line.push_back(L' ');
    command_line.append(QuoteArgument(Widen(argument)));
  }
  return command_line;
}
#endif

}  // namespace

absl::StatusOr<std::filesystem::path> FindExecutableOnPath(
    std::string_view executable_name) {
#ifdef _WIN32
  const std::wstring name = Widen(executable_name);
  if (name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos) {
    return absl::InvalidArgumentError("executable name is not valid UTF-8");
  }
  std::array<wchar_t, 32768> path_environment{};
  const DWORD length = GetEnvironmentVariableW(
      L"PATH", path_environment.data(),
      static_cast<DWORD>(path_environment.size()));
  if (length == 0 || length >= path_environment.size()) {
    return absl::NotFoundError("PATH is empty or too large to inspect safely");
  }
  std::wstring executable_filename = name;
  if (!executable_filename.ends_with(L".exe")) {
    executable_filename.append(L".exe");
  }
  std::wstring_view remaining(path_environment.data(), length);
  while (!remaining.empty()) {
    const std::size_t separator = remaining.find(L';');
    std::wstring_view entry = remaining.substr(0, separator);
    remaining = separator == std::wstring_view::npos
                    ? std::wstring_view()
                    : remaining.substr(separator + 1);
    while (!entry.empty() &&
           (entry.front() == L' ' || entry.front() == L'\t')) {
      entry.remove_prefix(1);
    }
    while (!entry.empty() &&
           (entry.back() == L' ' || entry.back() == L'\t')) {
      entry.remove_suffix(1);
    }
    if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"') {
      entry.remove_prefix(1);
      entry.remove_suffix(1);
    }
    const std::filesystem::path directory(entry);
    if (entry.empty() || !directory.is_absolute()) continue;
    const std::filesystem::path candidate = directory / executable_filename;
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error) continue;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(candidate, error);
    if (!error) return canonical;
  }
  return absl::NotFoundError(absl::StrCat(
      executable_name, " was not found in an absolute PATH directory; install "
                       "FFmpeg and open a new terminal or Visual Studio "
                       "instance"));
#else
  return absl::UnimplementedError(
      "external process discovery is not implemented on this platform");
#endif
}

absl::StatusOr<ProcessResult> RunProcessAndCaptureOutput(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments) {
#ifdef _WIN32
  SECURITY_ATTRIBUTES security = {.nLength = sizeof(SECURITY_ATTRIBUTES),
                                  .lpSecurityDescriptor = nullptr,
                                  .bInheritHandle = TRUE};
  HANDLE raw_read = nullptr;
  HANDLE raw_write = nullptr;
  if (!CreatePipe(&raw_read, &raw_write, &security, 0)) {
    return absl::UnknownError(absl::StrCat(
        "cannot create process output pipe: ", GetLastError()));
  }
  Handle read_pipe(raw_read);
  Handle write_pipe(raw_write);
  if (!SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0)) {
    return absl::UnknownError(absl::StrCat(
        "cannot configure process output pipe: ", GetLastError()));
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = write_pipe.get();
  startup.hStdError = write_pipe.get();
  PROCESS_INFORMATION process{};
  std::wstring command_line = BuildCommandLine(executable, arguments);
  if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr,
                      TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) {
    return absl::UnknownError(absl::StrCat(
        "cannot start ", executable.string(), ": Windows error ",
        GetLastError()));
  }
  Handle process_handle(process.hProcess);
  Handle thread_handle(process.hThread);
  write_pipe.release();
  CloseHandle(raw_write);

  std::string output;
  bool output_too_large = false;
  std::array<char, 4096> buffer{};
  for (;;) {
    DWORD bytes_read = 0;
    if (!ReadFile(read_pipe.get(), buffer.data(),
                  static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)) {
      const DWORD error = GetLastError();
      if (error == ERROR_BROKEN_PIPE) break;
      return absl::UnknownError(
          absl::StrCat("cannot read process output: Windows error ", error));
    }
    if (bytes_read == 0) break;
    const std::size_t available =
        kMaximumCapturedOutputBytes - output.size();
    const std::size_t to_copy =
        std::min<std::size_t>(available, bytes_read);
    output.append(buffer.data(), to_copy);
    if (to_copy < bytes_read) output_too_large = true;
  }

  if (WaitForSingleObject(process_handle.get(), INFINITE) != WAIT_OBJECT_0) {
    return absl::UnknownError(absl::StrCat(
        "cannot wait for process: Windows error ", GetLastError()));
  }
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
    return absl::UnknownError(absl::StrCat(
        "cannot read process exit code: Windows error ", GetLastError()));
  }
  if (output_too_large) {
    return absl::ResourceExhaustedError(
        "external process produced more than 1 MiB of diagnostic output");
  }
  return ProcessResult{.exit_code = exit_code, .output = std::move(output)};
#else
  return absl::UnimplementedError(
      "external process execution is not implemented on this platform");
#endif
}

}  // namespace racevideo
