cmake_minimum_required(VERSION 3.28)

project(RaceVideo VERSION 0.1.0 LANGUAGES C CXX)

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
find_package(Protobuf CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
  gpmf_parser_source
  GIT_REPOSITORY https://github.com/gopro/gpmf-parser.git
  GIT_TAG e40f6d9d3d7d17c9641c26d60e56d2927151d490
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR racevideo_no_subdirectory)
FetchContent_MakeAvailable(gpmf_parser_source)
add_library(gpmf_parser STATIC
  ${gpmf_parser_source_SOURCE_DIR}/GPMF_parser.c
  ${gpmf_parser_source_SOURCE_DIR}/GPMF_utils.c)
target_include_directories(gpmf_parser PUBLIC ${gpmf_parser_source_SOURCE_DIR})

FetchContent_Declare(
  stb_source
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG f58f558c120e9b32c217290b80bad1a0729fbb2c
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(stb_source)
add_library(racevideo_stb STATIC src/renderer/stb_image_write.c)
target_include_directories(racevideo_stb PUBLIC ${stb_source_SOURCE_DIR})

add_library(racevideo_core
  src/cli/options.cc
  src/cli/options.h
  src/common/run.cc
  src/common/run.h
  src/ffmpeg/process.cc
  src/ffmpeg/process.h
  src/ffmpeg/video_probe.cc
  src/ffmpeg/video_probe.h
  src/ffmpeg/video_encoder.cc
  src/ffmpeg/video_encoder.h
  src/overlay/overlay_data.cc
  src/overlay/overlay_data.h
  src/parser/mp4_gpmf.cc
  src/parser/mp4_gpmf.h
  src/parser/mp4_gpmf_index.cc
  src/parser/gpmf_metadata.cc
  src/parser/gpmf_metadata.h
  src/parser/telemetry_decoder.cc
  src/parser/telemetry_decoder.h
  src/parser/telemetry_protobuf.cc
  src/parser/telemetry_protobuf.h
  src/renderer/debug_renderer.cc
  src/renderer/debug_renderer.h
  src/statistics/gps_statistics.cc
  src/statistics/gps_statistics.h
  src/telemetry/axis_mapping.cc
  src/telemetry/axis_mapping.h
  src/telemetry/g_force.cc
  src/telemetry/g_force.h
  src/telemetry/mount_orientation.cc
  src/telemetry/mount_orientation.h
  src/telemetry/telemetry.h)
protobuf_generate(
  TARGET racevideo_core
  LANGUAGE cpp
  PROTOS proto/telemetry.proto)
target_link_libraries(
  racevideo_core
  PUBLIC racevideo_options absl::flags absl::flags_parse absl::status
         absl::statusor absl::strings absl::time gpmf_parser
         protobuf::libprotobuf racevideo_stb)
target_include_directories(
  racevideo_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR})

add_executable(racevideo src/cli/main.cc)
target_link_libraries(
  racevideo
  PRIVATE racevideo_core racevideo_options absl::flags_parse absl::log
          absl::log_initialize absl::status)

if(RACEVIDEO_BUILD_TESTS)
  include(CTest)
  find_package(GTest CONFIG REQUIRED)

  add_executable(
    racevideo_tests
    tests/axis_mapping_test.cc
    tests/debug_renderer_test.cc
    tests/video_probe_test.cc
    tests/gpmf_metadata_test.cc
    tests/g_force_test.cc
    tests/gps_statistics_test.cc
    tests/mount_orientation_test.cc
    tests/overlay_data_test.cc
    tests/mp4_gpmf_test.cc
    tests/options_test.cc
    tests/telemetry_test.cc)
  target_link_libraries(
    racevideo_tests
    PRIVATE racevideo_core racevideo_options GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(racevideo_tests)
endif()
