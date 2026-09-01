#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "cli/options.h"
#include "common/run.h"

int main(int argc, char* argv[]) {
  const auto start_time = std::chrono::steady_clock::now();
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

  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     start_time)
                                     .count();
  const int elapsed_minutes = static_cast<int>(elapsed_seconds / 60.0);
  const double remaining_seconds =
      elapsed_seconds - elapsed_minutes * 60.0;
  std::cout << std::fixed << std::setprecision(2)
            << "Elapsed time: " << elapsed_seconds << " total seconds ("
            << elapsed_minutes << " minutes " << remaining_seconds
            << " seconds).\n";

  return EXIT_SUCCESS;
}
