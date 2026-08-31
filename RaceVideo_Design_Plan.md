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
-   Treats FFmpeg as a user-supplied external tool; RaceVideo does not
    bundle or redistribute FFmpeg binaries.

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

Initial overlay widgets:

-   Speed readout
-   Compass heading derived from the GPS path
-   Filtered lateral/longitudinal acceleration indicator
-   North-up, aspect-preserving full-path map in a bounded panel. Draw the
    unexplored portion white and recolor the explored portion blue as the
    shared video playhead advances.

Resolution

Matches source video

Examples

3840x2160

1920x1080

------------------------------------------------------------------------

# FFmpeg Integration

Do NOT implement video codecs.

Do NOT bundle or redistribute FFmpeg with RaceVideo. FFmpeg is an external
runtime dependency installed separately by the user. RaceVideo locates a
user-configured or system-installed `ffmpeg` executable and communicates with
it only as a child process through standard pipes.

This subprocess boundary is a deliberate architecture and distribution
decision. Packaging workflows and release artifacts must not download, embed,
install, or ship an FFmpeg executable on the user's behalf.

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

Export compact, versioned Protobuf telemetry (`.rvt`); retain JSON as a
human-readable inspection format

------------------------------------------------------------------------

## Phase 3

Statistics

Display-oriented filtered G-force while preserving raw IMU data

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
-   Interactive telemetry viewer and 3D sensor visualization
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

# Future Telemetry Visualization

Build an optional `racevideo-viewer` application that reads `.rvt` files and
uses the same telemetry, filtering, and synchronization libraries as the main
RaceVideo pipeline. Keep it as a separate executable so the command-line
extractor and renderer do not require a desktop UI or 3D runtime.

The viewer should synchronize all panels to a shared playhead:

-   Video preview and frame-accurate timeline
-   Accelerometer and gyroscope component waveforms
-   A rotatable camera/vehicle model with acceleration and angular-velocity
    vector arrows
-   Short vector trails so impulses and oscillations remain visible
-   Frequency spectrograms for identifying vibration, engine harmonics,
    surface roughness, and impacts
-   RMS/peak envelopes over selectable time windows
-   GPS track, speed, altitude, and event markers
-   Controls for time scale, gain, filtering, pause, scrubbing, and looped
    regions

Do not present raw camera components as longitudinal, lateral, or vertical
vehicle acceleration. First normalize the camera model's encoded sensor order,
then apply only a coarse orthogonal mounting rotation. Assume the camera points
forward and classify its roll as upright, upside down, 90 degrees left, or 90
degrees right. Prefer an explicit orientation metadata field when available;
otherwise choose the nearest orientation from the long-term gravity direction.
Do not attempt fine tilt correction, GPS-based mounting inference, or arbitrary
3D calibration in the initial implementation.

Recommended implementation path:

1.  Add reusable signal-processing primitives: resampling, low/high-pass
    filters, RMS/peak windows, and FFT/spectrogram generation.
2.  Prototype an interactive viewer with Dear ImGui and ImPlot. Render the
    initial 3D axes, vectors, trails, and simple camera box directly through
    the selected graphics backend; avoid adding a full scene engine until a
    demonstrated requirement justifies it.
3.  Add coarse orthogonal mounting orientation for the 3D model. Fine-angle
    calibration and sensor fusion remain optional future work.
4.  Reuse simplified views as video widgets: a G-ball/vector trail, three-axis
    strip chart, vibration intensity bar, and impact markers.
5.  If full 3D output is later valuable in rendered videos, add an off-screen
    renderer behind an interface rather than coupling it to FFmpeg or the
    telemetry model.

Dear ImGui and ImPlot are suitable for a diagnostic tool because they support
interactive, real-time plotting with minimal integration overhead. A full 3D
engine such as Filament is intentionally deferred: it is capable and
cross-platform, but heavier than needed for arrows, axes, trails, and a simple
camera model.

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
