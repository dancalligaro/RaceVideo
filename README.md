# RaceVideo

RaceVideo is a modern C++20 application for extracting GoPro telemetry and
rendering racing overlays into video through FFmpeg.

## Development setup

Requirements:

- Visual Studio 2026 with the **Desktop development with C++** workload
- The vcpkg installation bundled with Visual Studio

Open **Developer PowerShell for VS 2026**, then configure and build:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The project follows the Google C++ style, uses Abseil status-based error
handling, and compiles application targets with C++ exceptions disabled.

## Current milestone

The CLI validates its input and scans the MP4 box hierarchy for the `gpmd`
sample description that identifies a GoPro GPMF telemetry track. Telemetry is
modeled as independent, timestamped sensor streams because GPS and inertial
sensors run at different rates.
