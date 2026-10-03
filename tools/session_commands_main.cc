#include <iostream>
#include <string>

#include "session_commands.h"

int main(int argc, char* argv[]) {
  racevideo::SessionCommandOptions options;
#ifdef _WIN32
  options.powershell = true;
#endif
  const auto usage = [] {
    std::cout << "Usage: racevideo_commands --folder=DIR --defaults=FILE\n"
                 "  [--output-prefix=PREFIX] [--platform=auto|linux|windows|macos]\n"
                 "  [--racevideo=EXECUTABLE]\n\n"
                 "Scan one folder and print one Bash/PowerShell command per session.\n"
                 "Defaults: flag=value, one per line; # comments and blank lines allowed.\n"
                 "Defaults may also set racevideo and output_prefix; CLI values override them.\n"
                 "Executable/prefix paths beginning with ~/ expand to the host home directory.\n"
                 "Output prefix defaults to overlay-; paths in it are relative to script execution.\n"
                 "Windows emits PowerShell; Linux/macOS emit Bash. Platform defaults to auto.\n"
                 "GoPro naming: GOPR####/GPnn####, GHnn####, GXnn#### (.MP4, case-insensitive).\n"
                 "Commands go to stdout; errors go to stderr. Videos are never processed here.\n";
  };
  for (int i = 1; i < argc; ++i) {
    std::string argument = argv[i];
    if (argument == "--help" || argument == "-h") {
      usage();
      return 0;
    }
    const auto equal = argument.find('=');
    const std::string key = argument.substr(0, equal);
    std::string value;
    if (equal != std::string::npos) value = argument.substr(equal + 1);
    else if (i + 1 < argc && !std::string(argv[i + 1]).starts_with("--")) value = argv[++i];
    else {
      std::cerr << "Missing value for " << key << '\n';
      return 1;
    }
    if (key == "--folder") options.folder = value;
    else if (key == "--defaults") options.defaults = value;
    else if (key == "--output-prefix") options.output_prefix = value;
    else if (key == "--racevideo") options.executable = value;
    else if (key == "--platform") {
      if (value == "windows") options.powershell = true;
      else if (value == "linux" || value == "macos") options.powershell = false;
      else if (value == "auto") {
#ifdef _WIN32
        options.powershell = true;
#else
        options.powershell = false;
#endif
      } else {
        std::cerr << "Platform must be auto, linux, windows, or macos\n";
        return 1;
      }
    } else {
      std::cerr << "Unknown option: " << key << '\n';
      return 1;
    }
  }
  if (options.folder.empty() || options.defaults.empty()) {
    std::cerr << "--folder and --defaults are required; use --help for usage\n";
    return 1;
  }
  const auto commands = racevideo::GenerateSessionCommands(options);
  if (!commands.ok()) {
    std::cerr << commands.status() << '\n';
    return 1;
  }
  std::cout << *commands;
  if (!std::cout) return 1;
  return 0;
}
