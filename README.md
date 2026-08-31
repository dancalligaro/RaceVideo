# RaceVideo

RaceVideo is a modern C++20 application for extracting GoPro telemetry and
rendering racing overlays into video through FFmpeg.

RaceVideo is an independent project and is not affiliated with, sponsored by,
or endorsed by GoPro, Inc. GoPro is a trademark of GoPro, Inc.

## Development setup

Requirements:

- Visual Studio 2026 with the **Desktop development with C++** workload
- The vcpkg installation bundled with Visual Studio

FFmpeg will be a separately installed runtime dependency when video rendering
is implemented. RaceVideo does not bundle, download, install, or redistribute
FFmpeg. Users will supply an `ffmpeg` executable through their system `PATH` or
a future explicit configuration option. RaceVideo will invoke it as a separate
process and communicate through standard pipes.

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

Inspect all embedded GPMF stream keys and sample counts:

```powershell
racevideo.exe --input="video.mp4" --inspect
```

Inspect the primary video stream with the separately installed `ffprobe`
executable:

```powershell
racevideo.exe --input="video.mp4" --inspect_video
```

This reports video dimensions, frame rate, duration, and whether an audio
stream is present. RaceVideo resolves `ffprobe` through `PATH`, starts it
directly without a command shell, and treats a nonzero exit code or malformed
probe output as an error.

Extract the original metadata bytes without converting or discarding unknown
fields:

```powershell
racevideo.exe --input="video.mp4" --extract_gpmf="metadata.gpmf"
```

Decode GPS telemetry into a portable JSON file:

```powershell
racevideo.exe --input="video.mp4" --export_json="telemetry.json"
```

For normal use, export the same telemetry in RaceVideo's compact, versioned
Protobuf format:

```powershell
racevideo.exe --input="video.mp4" --export_telemetry="telemetry.rvt"
```

The public schema is stored in `proto/telemetry.proto`. JSON remains available
for debugging and interoperability, while `.rvt` is the preferred format for
RaceVideo processing and caches.

Validate and summarize an `.rvt` file without the original video:

```powershell
racevideo.exe --inspect_telemetry="telemetry.rvt"
```

GPS, accelerometer, and gyroscope timestamps are expressed as seconds from the
beginning of the metadata track. Inertial values are scaled to SI units. Their
three fields are named `component_0`, `component_1`, and `component_2` because
the camera-axis order varies between GoPro models; they must not yet be treated
as vehicle longitudinal, lateral, and vertical axes.

When the camera model and its documented GPMF component order are known, apply
an explicit mapping during export. Uppercase axes are positive and lowercase
axes are negated:

```powershell
racevideo.exe --input="video.mp4" --imu_axis_order="YxZ" `
  --export_telemetry="telemetry.rvt"
```

This converts the stored inertial values to camera `X,Y,Z` and records the
source mapping in the `.rvt` file. RaceVideo then uses the long-term gravity
direction to classify the mount as upright, upside down, left-side down, or
right-side down and applies only that exact orthogonal correction. Do not guess
the source order: known GoPro models use different orders. RaceVideo assumes
the camera points forward and does not correct small mounting-angle errors.

Normalized exports also contain a display-oriented G-force stream filtered at
5 Hz. Positive lateral values point right, positive longitudinal values point
forward, and vertical dynamic G has the resting 1 G removed. The original
high-frequency accelerometer samples remain in the file for later analysis.
Inspecting the `.rvt` reports directional peak G-force values.

Inspection also reports GPS distance, moving time, maximum speed, average
moving speed, and noise-resistant elevation gain. Statistics are calculated in
SI units; the CLI currently displays distance in kilometers and speed in km/h.

Generate transparent PNG overlay frames for a short video range while
developing the renderer:

```powershell
racevideo.exe --input="video.mp4" --imu_axis_order="ZXY" `
  --render_frames="debug-frames" --start_seconds=60 --duration_seconds=5 `
  --render_fps=30 --render_width=1920 --render_height=1080
```

Each frame contains speed, compass heading, a G-force target, and the complete
track. The unexplored track is white, the explored portion is blue, and the
current position is red. RaceVideo renders only the requested interval, limits
one invocation to 10,000 frames, and refuses to overwrite existing numbered
frames. These PNGs contain transparency and do not require FFmpeg or decode the
video image; they are intended for inspecting the overlay layer and will later
be composited over video by the external FFmpeg executable.

## Privacy

GoPro metadata can contain precise GPS locations, timestamps, device details,
and other sensitive information. Review extracted metadata before sharing it.

## License

RaceVideo is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
Third-party components retain their own licenses and notices; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Unless explicitly stated otherwise, contributions submitted for inclusion in
RaceVideo are provided under the Apache License 2.0.
