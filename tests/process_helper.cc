#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc < 2) return 1;
  const std::string_view mode(argv[1]);
  if (mode == "arguments") {
    for (int index = 2; index < argc; ++index) {
      const std::string_view argument(argv[index]);
      std::printf("%zu:", argument.size());
      std::fwrite(argument.data(), 1, argument.size(), stdout);
      std::putchar('\n');
    }
  } else if (mode == "exit") {
    std::fputs("expected diagnostic", stderr);
    return 23;
  } else if (mode == "wait") {
    std::this_thread::sleep_for(std::chrono::seconds(120));
  } else if (mode == "signal") {
    std::raise(SIGTERM);
  } else if (mode == "flood") {
    const std::array<char, 4096> buffer{};
    for (int index = 0; index < 512; ++index) {
      std::fwrite(buffer.data(), 1, buffer.size(), stdout);
    }
  } else if (mode == "duplex") {
    // Fill the output pipe before reading input, exposing sequential I/O hangs.
    const std::array<char, 4096> output{};
    for (int index = 0; index < 32; ++index) {
      std::fwrite(output.data(), 1, output.size(), stdout);
    }
    std::fflush(stdout);
    std::array<unsigned char, 4096> buffer{};
    unsigned long long size = 0;
    unsigned long long sum = 0;
    for (;;) {
      const std::size_t count =
          std::fread(buffer.data(), 1, buffer.size(), stdin);
      if (count == 0) break;
      size += count;
      for (std::size_t index = 0; index < count; ++index) sum += buffer[index];
    }
    std::printf("bytes=%llu sum=%llu", size, sum);
  } else {
    return 2;
  }
  return 0;
}
