#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <system_error>
#include <thread>
#include <utility>

#include "absl/strings/str_cat.h"
#include "ffmpeg/process.h"

extern char** environ;

namespace racevideo {
namespace {

constexpr std::size_t kMaximumCapturedOutputBytes = 1024 * 1024;

absl::Status ProcessError(std::string_view operation, int error) {
  return absl::UnknownError(
      absl::StrCat(operation, ": ",
                   std::error_code(error, std::generic_category()).message()));
}

class FileDescriptor {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) : fd_(fd) {}
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}
  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      Reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~FileDescriptor() { Reset(); }
  int get() const { return fd_; }
  void Reset() {
    if (fd_ >= 0) close(std::exchange(fd_, -1));
  }

 private:
  int fd_ = -1;
};

struct Pipe {
  FileDescriptor read;
  FileDescriptor write;
};

absl::StatusOr<Pipe> CreatePipe() {
  int descriptors[2];
  if (pipe(descriptors) != 0) return ProcessError("cannot create pipe", errno);
  Pipe result{FileDescriptor(descriptors[0]), FileDescriptor(descriptors[1])};
  // Keep pipe ends above stderr, even when the caller has closed a standard
  // descriptor. This makes spawn dup2/close actions independent of fd ordering.
  for (FileDescriptor* fd : {&result.read, &result.write}) {
    const int duplicate = fcntl(fd->get(), F_DUPFD_CLOEXEC, 3);
    if (duplicate < 0) return ProcessError("cannot configure pipe", errno);
    *fd = FileDescriptor(duplicate);
  }
  return result;
}

class SpawnActions {
 public:
  SpawnActions() : error_(posix_spawn_file_actions_init(&actions_)) {}
  SpawnActions(const SpawnActions&) = delete;
  SpawnActions& operator=(const SpawnActions&) = delete;
  ~SpawnActions() {
    if (error_ == 0) posix_spawn_file_actions_destroy(&actions_);
  }
  int error() const { return error_; }
  posix_spawn_file_actions_t* get() { return &actions_; }

 private:
  posix_spawn_file_actions_t actions_{};
  int error_;
};

// A failed producer must not leave FFmpeg waiting for more input. Always reap
// the child, including on read/write errors. Keep the terminal's process group
// so Ctrl-C reaches both RaceVideo and FFmpeg.
class ChildProcess {
 public:
  explicit ChildProcess(pid_t pid) : pid_(pid) {}
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ~ChildProcess() {
    if (pid_ > 0) {
      Terminate();
      const auto ignored = Wait();
      (void)ignored;
    }
  }
  void Terminate() const {
    if (pid_ > 0) kill(pid_, SIGKILL);
  }
  absl::StatusOr<unsigned long> Wait() {
    int status = 0;
    pid_t result;
    do {
      result = waitpid(pid_, &status, 0);
    } while (result < 0 && errno == EINTR);
    const int error = errno;
    pid_ = -1;
    if (result < 0) return ProcessError("cannot wait for process", error);
    if (WIFEXITED(status))
      return static_cast<unsigned long>(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) {
      return static_cast<unsigned long>(128 + WTERMSIG(status));
    }
    return absl::UnknownError("process ended without an exit status");
  }

 private:
  pid_t pid_;
};

// Darwin can deliver a pipe's SIGPIPE to another thread. Suppress it on the
// descriptor there; on Linux block it on the writing thread and consume any
// newly generated pending signal before restoring the original mask.
// Neither approach changes the application's process-wide signal handlers.
class SuppressSigpipe {
 public:
  SuppressSigpipe(const SuppressSigpipe&) = delete;
  SuppressSigpipe& operator=(const SuppressSigpipe&) = delete;
  explicit SuppressSigpipe(int fd) {
#ifdef F_SETNOSIGPIPE
    if (fcntl(fd, F_SETNOSIGPIPE, 1) < 0) error_ = errno;
#else
    (void)fd;
    sigemptyset(&set_);
    sigaddset(&set_, SIGPIPE);
    error_ = pthread_sigmask(SIG_BLOCK, &set_, &previous_);
    if (error_ == 0) {
      sigset_t pending;
      sigpending(&pending);
      was_pending_ = sigismember(&pending, SIGPIPE) == 1;
    }
#endif
  }
  ~SuppressSigpipe() {
#ifndef F_SETNOSIGPIPE
    if (error_ != 0) return;
    sigset_t pending;
    sigpending(&pending);
    if (!was_pending_ && sigismember(&pending, SIGPIPE) == 1) {
      int signal = 0;
      sigwait(&set_, &signal);
    }
    pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
#endif
  }
  int error() const { return error_; }

 private:
  int error_ = 0;
#ifndef F_SETNOSIGPIPE
  sigset_t set_{};
  sigset_t previous_{};
  bool was_pending_ = false;
#endif
};

absl::StatusOr<ProcessResult> RunProcess(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, const InputProducer* producer) {
  if (executable.empty() ||
      executable.native().find('\0') != std::string::npos) {
    return absl::InvalidArgumentError(
        "executable path is empty or contains NUL");
  }
  std::vector<std::string> storage{executable.native()};
  for (const std::string& argument : arguments) {
    if (argument.find('\0') != std::string::npos) {
      return absl::InvalidArgumentError("process argument contains NUL");
    }
    storage.push_back(argument);
  }
  std::vector<char*> argv;
  for (std::string& argument : storage) argv.push_back(argument.data());
  argv.push_back(nullptr);

  absl::StatusOr<Pipe> output = CreatePipe();
  if (!output.ok()) return output.status();
  absl::StatusOr<Pipe> input = CreatePipe();
  if (!input.ok()) return input.status();
  SpawnActions actions;
  if (actions.error() != 0)
    return ProcessError("cannot prepare process", actions.error());
  for (const auto& [source, destination] :
       {std::pair{input->read.get(), STDIN_FILENO},
        std::pair{output->write.get(), STDOUT_FILENO},
        std::pair{output->write.get(), STDERR_FILENO}}) {
    const int error =
        posix_spawn_file_actions_adddup2(actions.get(), source, destination);
    if (error != 0) return ProcessError("cannot redirect process pipe", error);
  }
  for (int fd : {input->read.get(), input->write.get(), output->read.get(),
                 output->write.get()}) {
    const int error = posix_spawn_file_actions_addclose(actions.get(), fd);
    if (error != 0) return ProcessError("cannot close child pipe", error);
  }
  pid_t pid = -1;
  const int error = posix_spawn(&pid, executable.c_str(), actions.get(),
                                nullptr, argv.data(), environ);
  if (error != 0)
    return ProcessError("cannot start " + executable.string(), error);
  ChildProcess child(pid);
  input->read.Reset();
  output->write.Reset();

  std::string captured;
  bool too_large = false;
  absl::Status read_status;
  auto read_output = [&] {
    std::array<char, 4096> buffer;
    for (;;) {
      const ssize_t count =
          read(output->read.get(), buffer.data(), buffer.size());
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) {
        read_status = ProcessError("cannot read process output", errno);
        child.Terminate();
        return;
      }
      if (count == 0) return;
      const std::size_t size = static_cast<std::size_t>(count);
      const std::size_t to_copy =
          std::min(size, kMaximumCapturedOutputBytes - captured.size());
      captured.append(buffer.data(), to_copy);
      too_large = too_large || to_copy < size;
      // Continue draining after reaching the limit so the child cannot block.
    }
  };

  absl::Status producer_status;
  if (producer != nullptr) {
    std::thread reader(read_output);
    {
      SuppressSigpipe suppressed(input->write.get());
      if (suppressed.error() != 0) {
        producer_status =
            ProcessError("cannot suppress SIGPIPE", suppressed.error());
      } else {
        const ByteSink sink = [&](std::span<const std::uint8_t> bytes) {
          while (!bytes.empty()) {
            const std::size_t chunk =
                std::min<std::size_t>(bytes.size(), 1024 * 1024);
            const ssize_t written =
                write(input->write.get(), bytes.data(), chunk);
            if (written < 0 && errno == EINTR) continue;
            if (written < 0)
              return ProcessError("cannot write process input", errno);
            if (written == 0)
              return absl::UnknownError("process input pipe accepted no bytes");
            bytes = bytes.subspan(static_cast<std::size_t>(written));
          }
          return absl::OkStatus();
        };
        producer_status = (*producer)(sink);
      }
    }
    input->write.Reset();
    if (!producer_status.ok()) child.Terminate();
    reader.join();
  } else {
    input->write.Reset();  // Capture-only commands receive EOF on stdin.
    read_output();
  }
  const absl::StatusOr<unsigned long> exit_code = child.Wait();
  if (!exit_code.ok()) return exit_code.status();
  if (!read_status.ok()) return read_status;
  if (!producer_status.ok()) {
    return absl::Status(producer_status.code(),
                        absl::StrCat(producer_status.message(),
                                     captured.empty() ? "" : ": ", captured));
  }
  if (too_large) {
    return absl::ResourceExhaustedError(
        "external process produced more than 1 MiB of diagnostic output");
  }
  return ProcessResult{.exit_code = *exit_code, .output = std::move(captured)};
}

}  // namespace

absl::StatusOr<std::filesystem::path> FindExecutableOnPath(
    std::string_view executable_name) {
  if (executable_name.empty() ||
      executable_name.find('/') != std::string_view::npos ||
      executable_name.find('\0') != std::string_view::npos) {
    return absl::InvalidArgumentError(
        "executable name must be a nonempty filename");
  }
  const char* environment = std::getenv("PATH");
  std::string_view remaining = environment == nullptr ? "" : environment;
  while (!remaining.empty()) {
    const std::size_t separator = remaining.find(':');
    const std::filesystem::path directory(remaining.substr(0, separator));
    remaining = separator == std::string_view::npos
                    ? std::string_view{}
                    : remaining.substr(separator + 1);
    // Match Windows' policy: do not run tools from implicit current
    // directories.
    if (!directory.is_absolute()) continue;
    const std::filesystem::path candidate = directory / executable_name;
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error ||
        access(candidate.c_str(), X_OK) != 0)
      continue;
    const std::filesystem::path canonical =
        std::filesystem::canonical(candidate, error);
    if (!error) return canonical;
  }
  return absl::NotFoundError(
      absl::StrCat(executable_name,
                   " was not found in an absolute PATH directory; install "
                   "FFmpeg and add its executable directory to PATH"));
}

absl::StatusOr<ProcessResult> RunProcessAndCaptureOutput(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments) {
  return RunProcess(executable, arguments, nullptr);
}

absl::StatusOr<ProcessResult> RunProcessWithInput(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, const InputProducer& producer) {
  return RunProcess(executable, arguments, &producer);
}

}  // namespace racevideo
