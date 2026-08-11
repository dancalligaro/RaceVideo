#include <cstdlib>
#include <iostream>

#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "cli/options.h"
#include "common/run.h"

int main(int argc, char* argv[]) {
  absl::InitializeLog();

  absl::StatusOr<racevideo::Options> options =
      racevideo::ParseOptions(argc, argv);
  if (!options.ok()) {
    std::cerr << options.status() << '\n';
    return EXIT_FAILURE;
  }

  const absl::Status status = racevideo::Run(*options);
  if (!status.ok()) {
    std::cerr << status << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

