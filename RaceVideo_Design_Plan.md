# RaceVideo -- Design Plan

## Goal

Build a **modern C++20** application for Windows that:

-   Reads GoPro MP4 files.
-   Extracts embedded telemetry (GPS, speed, altitude, heading,
    G-forces).
-   Renders customizable racing overlays.
-   Streams rendered overlay frames directly to FFmpeg.
-   Uses GPU hardware decoding/encoding (NVENC, Intel Quick Sync, etc.).
-   Avoids unnecessary temporary files.
-   Is modular and extensible.

------------------------------------------------------------------------

# High-Level Architecture

``` text
           GoPro MP4
                │
                ▼
      Telemetry Extraction
                │
                ▼
      Telemetry Data Model
                │
      ┌─────────┴─────────┐
      ▼                   ▼
Statistics Engine     Overlay Renderer
      │                   │
      └─────────┬─────────┘
                ▼
      RGBA Frame Generator
                │
                ▼
         FFmpeg (stdin)
                │
                ▼
 Hardware Decode → Overlay → Hardware Encode
                │
                ▼
          Final MP4
```

------------------------------------------------------------------------

# Design Principles

-   Modern C++20
-   RAII everywhere
-   No global state
-   Small focused classes
-   Testable modules
-   Dependency injection where appropriate
-   CMake build
-   Cross-platform friendly where practical

------------------------------------------------------------------------

# Repository Layout

``` text
racevideo/
│
├── CMakeLists.txt
├── docs/
├── samples/
├── tests/
│
├── src/
│   ├── cli/
│   ├── parser/
│   ├── telemetry/
│   ├── renderer/
│   ├── ffmpeg/
│   ├── overlay/
│   ├── statistics/
│   └── common/
│
└── third_party/
```

------------------------------------------------------------------------

# Modules

## CLI

Responsibilities

-   Parse arguments
-   Load configuration
-   Validate files
-   Select encoder

Example

``` text
racevideo.exe input.mp4 \
    --encoder nvenc \
    --codec hevc \
    --theme minimal
```

Libraries

-   CLI11

------------------------------------------------------------------------

## Parser

Responsibilities

-   Read MP4
-   Locate GPMF metadata
-   Decode telemetry streams

Output

``` cpp
struct TelemetrySample
{
    double timestamp;
    double latitude;
    double longitude;
    double speed;
    double altitude;
    double heading;
};
```

Future

-   Multiple camera support

------------------------------------------------------------------------

## Telemetry Engine

Provides

Interpolation

Distance

Bearing

Acceleration

Lap detection

Filtering

Statistics

------------------------------------------------------------------------

## Statistics Engine

Computes

Maximum speed

Average speed

Distance

Moving time

Elevation gain

Lap times

Corner G-force

Ideal lap

Future

Sector analysis

------------------------------------------------------------------------

## Renderer

Responsibilities

Draw

Text

Needles

Bars

Maps

Icons

Gauges

Output

RGBA frame

Possible libraries

-   Skia
-   NanoVG
-   Blend2D

Start with

Blend2D

Reasons

-   High quality
-   Modern
-   Easy API
-   CPU renderer
-   Excellent text rendering

------------------------------------------------------------------------

# Overlay Pipeline

For each video frame

``` text
Telemetry sample
        │
        ▼
Renderer
        │
RGBA Frame
```

Resolution

Matches source video

Examples

3840x2160

1920x1080

------------------------------------------------------------------------

# FFmpeg Integration

Do NOT implement video codecs.

Instead

Launch FFmpeg.

Feed overlay frames through stdin.

Example concept

``` text
Renderer

↓

raw RGBA frames

↓

stdin pipe

↓

FFmpeg overlay filter

↓

NVENC
```

Advantages

No PNG sequence

No disk I/O

Constant memory usage

Easy streaming

------------------------------------------------------------------------

# Hardware Acceleration

Supported encoders

-   h264_nvenc
-   hevc_nvenc
-   av1_nvenc (supported GPUs)
-   h264_qsv
-   hevc_qsv
-   libx264
-   libx265

Supported hardware decode

-   cuda
-   d3d11va
-   qsv

Encoder abstraction

``` cpp
enum class Encoder
{
    Software,
    Nvenc,
    QuickSync
};
```

------------------------------------------------------------------------

# Configuration

JSON

Example

``` json
{
  "theme":"minimal",
  "units":"mph",
  "speedometer":true,
  "track_map":true
}
```

Library

nlohmann/json

------------------------------------------------------------------------

# Themes

Future directory

``` text
themes/

minimal/

audi/

motorsport/

classic/
```

Each theme

Fonts

Colors

Needles

Gauge layout

Images

JSON

------------------------------------------------------------------------

# Development Phases

## Phase 1

Project setup

CMake

CLI

Logging

Tests

------------------------------------------------------------------------

## Phase 2

Telemetry extraction

Read GoPro metadata

Print samples

Export JSON

------------------------------------------------------------------------

## Phase 3

Statistics

Maximum speed

Distance

Lap detection

------------------------------------------------------------------------

## Phase 4

Rendering

Speedometer

Text

Needles

Map

------------------------------------------------------------------------

## Phase 5

Streaming pipeline

Generate RGBA frames

Pipe to FFmpeg

Produce video

------------------------------------------------------------------------

## Phase 6

Hardware acceleration

NVENC

Quick Sync

Benchmarking

------------------------------------------------------------------------

## Phase 7

Themes

Custom gauges

Track maps

Animated widgets

------------------------------------------------------------------------

# Testing

Unit tests

Telemetry parser

Interpolation

Distance math

Lap detection

Renderer

Integration tests

Sample GoPro clips

------------------------------------------------------------------------

# Stretch Goals

-   Live preview window
-   Timeline editor
-   Plugin system
-   Lua scripting
-   OBD-II integration
-   RaceBox integration
-   Garmin integration
-   AIM integration
-   Assetto Corsa telemetry import
-   Automatic highlight detection

------------------------------------------------------------------------

# Learning Objectives

By the end of this project we should have practical experience with:

-   Modern C++20
-   CMake
-   RAII
-   Smart pointers
-   STL containers
-   Binary parsing
-   JSON
-   Graphics rendering
-   Inter-process communication
-   FFmpeg integration
-   GPU video encoding
-   Software architecture
-   Unit testing
-   Performance profiling
