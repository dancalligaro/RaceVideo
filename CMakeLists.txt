cmake_minimum_required(VERSION 3.28)

project(RaceVideo VERSION 0.1.0 LANGUAGES CXX)

option(RACEVIDEO_BUILD_TESTS "Build RaceVideo tests" ON)

add_library(racevideo_options INTERFACE)
target_compile_features(racevideo_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(racevideo_options INTERFACE /W4 /WX /permissive- /EHs-c-)
  target_compile_definitions(racevideo_options INTERFACE _HAS_EXCEPTIONS=0)
else()
  target_compile_options(
    racevideo_options
    INTERFACE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)
endif()

find_package(absl CONFIG REQUIRED)

add_library(racevideo_core
  src/cli/options.cc
  src/cli/options.h
  src/common/run.cc
  src/common/run.h)
target_link_libraries(
  racevideo_core
  PUBLIC racevideo_options absl::flags absl::flags_parse absl::status
         absl::statusor absl::strings)
target_include_directories(racevideo_core PUBLIC src)

add_executable(racevideo src/cli/main.cc)
target_link_libraries(
  racevideo
  PRIVATE racevideo_core racevideo_options absl::flags_parse absl::log
          absl::log_initialize absl::status)

if(RACEVIDEO_BUILD_TESTS)
  include(CTest)
  find_package(GTest CONFIG REQUIRED)

  add_executable(racevideo_tests tests/options_test.cc)
  target_link_libraries(
    racevideo_tests
    PRIVATE racevideo_core racevideo_options GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(racevideo_tests)
endif()

