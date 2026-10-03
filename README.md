# RaceVideo

RaceVideo is a modern C++20 application for extracting GoPro telemetry and
rendering racing overlays into video through FFmpeg.

RaceVideo is an independent project and is not affiliated with, sponsored by,
or endorsed by GoPro, Inc. GoPro is a trademark of GoPro, Inc.

## Run the prebuilt Linux executable

The repository includes an optimized [Linux x86-64 executable](bin/linux-x86_64/racevideo)
(AMD64, for Intel/AMD 64-bit CPUs), built on Debian 13 with GCC 14.2.
It requires glibc 2.38 or newer and `libstdc++.so.6` providing
`GLIBCXX_3.4.32` (GCC 13.2 or newer runtime). It has been smoke-tested on
Debian 13; other distributions must provide compatible runtimes. Older systems
and ARM64 require a source build using the development instructions below.

On Debian 13, install the runtime dependencies:

```sh
sudo apt-get update
sudo apt-get install libc6 libstdc++6 libgcc-s1 ffmpeg
```

FFmpeg supplies both `ffmpeg` and `ffprobe`; both must be on `PATH`.
Software video encoding requires FFmpeg with `libx264`. Abseil, Protocol
Buffers, utf8-range, the GoPro parser, and stb are linked into the executable;
no separate packages for those libraries or build tools are needed.

From a downloaded or cloned copy of this repository:

```sh
chmod +x bin/linux-x86_64/racevideo
./bin/linux-x86_64/racevideo --version
./bin/linux-x86_64/racevideo --helpfull
./bin/linux-x86_64/racevideo --input="video.mp4" --inspect_video
./bin/linux-x86_64/racevideo --input="video.mp4" --imu_axis_order="ZXY" \
  --output_video="preview.mp4" --duration_seconds=10 --output_width=400 \
  --speed_unit=kmh
```

Use the correct `--imu_axis_order` for your camera; `ZXY` is an example.
To install the executable on your system `PATH`:

```sh
sudo install -m 0755 bin/linux-x86_64/racevideo /usr/local/bin/racevideo
racevideo --helpfull
```

Replace the installed file with the same command when updating. Remove it
with `sudo rm /usr/local/bin/racevideo` to uninstall. Optional VA-API and NVIDIA
hardware encoding require the drivers and FFmpeg capabilities described in
[Linux development setup](#linux).

The executable is distributed with [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), including the linked
third-party license texts. Preserve these notices when redistributing it.

To rebuild the checked-in executable on x86-64 Linux with GCC, set
`VCPKG_ROOT` to your bootstrapped vcpkg checkout and run:

```sh
python3 scripts/build_linux_binary.py
```

This builds the application and its static dependencies with source-path
remapping, strips the executable, and checks for local build paths before
replacing `bin/linux-x86_64/racevideo`. It requires the Linux build tools below,
Python 3, and `strip`/`strings` from binutils. Recheck runtime requirements
when changing the compiler or build distribution.

`--version` prints the release version and a build ID, for example
`RaceVideo 0.1 (build 267efc)`. The ID is the first six characters of Git HEAD
at build time and refreshes on every build. A `-dirty` suffix means tracked
source files have uncommitted changes (the distributed `bin/` directory is
excluded). Builds without Git metadata report `unknown`.
Bump the version in `project(RaceVideo VERSION ...)` in `CMakeLists.txt` when
preparing a new release; commits and recompiles refresh the build ID automatically.
For exact commit traceability, commit source changes before building the binary.

## Development setup

### macOS

Install Apple's Command Line Tools (`xcode-select --install`) and Homebrew,
then install the build tools and the separate FFmpeg runtime:

```sh
brew install cmake ninja pkg-config ffmpeg
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
```

If you already have a standalone vcpkg checkout, set `VCPKG_ROOT` to it instead.
CMake 3.28 or newer is required. From the RaceVideo directory, build and test:

```sh
cmake --preset debug -DVCPKG_TARGET_TRIPLET=arm64-osx -DRACEVIDEO_FFMPEG_TESTS=ON
cmake --build --preset debug
ctest --preset debug
```

Use `x64-osx` instead of `arm64-osx` on an Intel Mac. Build each architecture
in its own build directory; these commands produce a native executable, not a
universal binary. vcpkg installs the library versions pinned in `vcpkg.json`.
The first configure also downloads the pinned GoPro parser and stb sources.

For an optimized executable:

```sh
cmake --preset release -DVCPKG_TARGET_TRIPLET=arm64-osx
cmake --build --preset release
./build/release/racevideo --input="video.mp4" --inspect_video
./build/release/racevideo --input="video.mp4" --imu_axis_order="ZXY" \
  --output_video="preview.mp4" --duration_seconds=10 --output_width=400 \
  --video_encoder=videotoolbox --video_pipeline=software --speed_unit=kmh
```

Use the correct `--imu_axis_order` for your camera; `ZXY` is an example.
The Windows examples below also apply on macOS: replace `racevideo.exe` with
`./build/release/racevideo` and PowerShell's backtick line continuations with
shell backslashes.

The software encoder requires FFmpeg with `libx264`. On macOS,
`--video_encoder=videotoolbox` uses Apple's hardware H.264 encoder while decode,
scaling, and overlay composition remain on the CPU. It requires an FFmpeg build
containing `h264_videotoolbox`; Homebrew's FFmpeg provides it. NVIDIA/CUDA
options require NVIDIA hardware and are not supported on Apple Silicon.

### Linux

On Debian/Ubuntu, install the build tools and FFmpeg runtime:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build pkg-config git curl zip unzip tar ffmpeg
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
cmake --preset debug -DVCPKG_TARGET_TRIPLET=x64-linux -DRACEVIDEO_FFMPEG_TESTS=ON
cmake --build --preset debug
ctest --preset debug
cmake --preset release -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build --preset release
```

CMake 3.28+ and a C++20 compiler are required; upgrade CMake separately if your
distribution provides an older version. Use `arm64-linux` on ARM64.
Linux and macOS share the POSIX process backend. FFmpeg and ffprobe must be
on `PATH`; FFmpeg must include `libx264` for software encoding.

For Intel and AMD hardware H.264 encoding, use VA-API:

```sh
./build/release/racevideo --input="video.mp4" --imu_axis_order="ZXY" \
  --output_video="preview.mp4" --duration_seconds=10 --output_width=400 \
  --video_encoder=vaapi --vaapi_device=/dev/dri/renderD128 --speed_unit=kmh
```

VA-API requires FFmpeg with `h264_vaapi` and `hwupload`, a GPU supporting H.264
encoding, and its installed VA-API driver (typically Intel's media driver or
Mesa's VA-API driver for AMD). Your user must have access to the DRM render
node; distributions commonly manage this through the `render` group. Use
`vainfo --display drm --device /dev/dri/renderD128` to check driver support.
Select another node with `--vaapi_device` on systems with multiple GPUs.
The default device is `/dev/dri/renderD128`. Decode, scaling, and transparent
overlay composition run on the CPU; the result is converted to NV12 and
uploaded for hardware encoding. Use `--video_pipeline=software` (the default).
Missing encoders, devices, or driver support produce an error; RaceVideo does
not silently fall back to software. See [FFmpeg's hardware device documentation](https://ffmpeg.org/ffmpeg.html#Advanced-Video-options).

For NVIDIA GPUs, the existing `--video_encoder=nvidia` also works on Linux
with the NVIDIA driver and an NVENC-enabled FFmpeg build. Add
`--video_pipeline=nvidia` to enable CUDA decode, scale, and overlay processing;
this also requires FFmpeg's CUDA filters as described below.
Use `ffmpeg -encoders` and `ffmpeg -filters` to inspect your installed build.

### Windows

Requirements:

- Visual Studio 2026 with the **Desktop development with C++** workload
- The vcpkg installation bundled with Visual Studio

FFmpeg is a separately installed runtime dependency for video rendering.
RaceVideo does not bundle, download, install, or redistribute FFmpeg. Users
supply the `ffmpeg` and `ffprobe` executables through their system `PATH`.
RaceVideo invokes them as separate processes and communicates through standard
pipes.

Open **Developer PowerShell for VS 2026**, then configure and build:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

To configure and build an optimized Release binary:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
cmake --preset release
cmake --build --preset release
```

The resulting executable is `build\release\racevideo.exe`.

The project follows the Google C++ style, uses Abseil status-based error
handling, and compiles application targets with C++ exceptions disabled.

### Tests and external processes

Normal builds include unit tests and process tests using a small test helper.
Set `-DRACEVIDEO_FFMPEG_TESTS=ON` during configuration to also generate test
videos and verify software overlay encoding, audio preservation, chapter
concatenation, and refusal to overwrite an existing output. These integration
tests require both `ffmpeg` and `ffprobe` on `PATH`; they do not need GoPro files.
CI runs these tests on Windows, macOS, and Linux. On Linux, set
`RACEVIDEO_TEST_VAAPI_DEVICE=/dev/dri/renderD128` when running `ctest` to also
exercise VA-API encoding for single and multiple chapters; these tests are
skipped by default because hosted CI runners do not provide a GPU.

External tools are started directly without a command shell. Executable lookup
uses absolute `PATH` directories and skips relative or empty entries. On POSIX,
capture-only commands receive EOF on stdin; streaming commands receive the
generated RGBA frames. The backend handles broken pipes and reaps children on
completion or errors. Terminal Ctrl-C reaches both processes through their
shared foreground process group. Unlike Windows job objects, POSIX does not
guarantee child cleanup if RaceVideo itself is forcibly killed with SIGKILL.

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

Each frame contains speed, a G-force target, and the complete track. The
unexplored track is white, the explored portion is blue, and a red arrow marks
the current position and heading. RaceVideo renders only the requested
interval, limits
one invocation to 10,000 frames, and refuses to overwrite existing numbered
frames. These PNGs contain transparency and do not require FFmpeg or decode the
video image; they are intended for inspecting the overlay layer.

Render the overlay directly into a new MP4 without creating intermediate image
files:

```powershell
racevideo.exe --input="video.mp4" --imu_axis_order="ZXY" `
  --output_video="video-overlay.mp4" --start_seconds=60 `
  --duration_seconds=10 --speed_unit="kmh"
```

RaceVideo streams transparent RGBA frames to the separately installed FFmpeg
process. FFmpeg composites them over the source, encodes H.264 video, and copies
the original audio stream. Omit `--duration_seconds` (or set it to zero) to
render from the requested start time through the remainder of the video.
Existing output files are never overwritten.
Speed is hidden unless `--speed_unit` is provided. Use `kmh` or `mph` for one
row, or an ordered pair such as `kmh,mph` or `mph,kmh` for two stacked rows.

For a faster, lower-resolution preview, set an even output width. RaceVideo
preserves the source aspect ratio and calculates an even output height:

```powershell
racevideo.exe --input="video.mp4" --imu_axis_order="ZXY" `
  --output_video="preview.mp4" --output_width=400 `
  --video_encoder="nvidia" --speed_unit="kmh,mph"
```

`--video_encoder="software"` is the default and uses `libx264`.
`--video_encoder="nvidia"` uses FFmpeg's `h264_nvenc` encoder and reports an
error when the installed FFmpeg does not provide it. On macOS,
`--video_encoder="videotoolbox"` uses `h264_videotoolbox` for hardware H.264
encoding with the default software pipeline. It reports an error if FFmpeg does
not provide the encoder or the system cannot create a hardware encoding
session. On Linux, `--video_encoder="vaapi"` uses `h264_vaapi` with the
software pipeline; see the Linux setup above for driver and device requirements.
`--output_width=0`, the
default, preserves the source resolution. Output widths must be even and at
least 160 pixels; RaceVideo does not upscale the source video.

With the NVIDIA encoder selected, the complete decode, scale, overlay, and
encode path can optionally remain on the GPU:

```powershell
racevideo.exe --input="video.mp4" --imu_axis_order="ZXY" `
  --output_video="gpu-preview.mp4" --output_width=400 `
  --video_encoder="nvidia" --video_pipeline="nvidia" `
  --speed_unit="kmh,mph"
```

This mode requires an H.264 or HEVC source and an FFmpeg build containing CUDA
hardware decoding plus `scale_cuda`, `overlay_cuda`, `hwupload_cuda`, and
`h264_nvenc`. Only the generated transparent overlay frames are uploaded from
the CPU. The default `--video_pipeline="software"` continues to use FFmpeg's
software decoder, scaler, and overlay filter and can be used for compatibility
or performance comparisons. Hardware processing is not guaranteed to be
faster: CUDA startup, pixel conversion, and uploading a full-frame overlay can
outweigh the decoding savings, particularly for low-resolution previews.

Before encoding, RaceVideo prints the input and selected output durations.
During encoding it reports frame progress at most once per second, together
with cumulative overlay-rendering, incremental-track, and FFmpeg pipe/wait
timings. A final summary also reports map preparation and initial track setup.
The complete white track is rasterized once and the blue travelled route is
updated incrementally as timestamps advance; it is not rebuilt from the
beginning for every frame. After a successful run RaceVideo prints elapsed
wall-clock time as both total seconds and minutes plus seconds, making preview
and encoder performance easy to compare.

Overlay rendering reuses frame buffers, caches gauge artwork and unchanged
readouts, and shares immutable track images between frames when the explored
route has not changed. Small layouts with overlapping widgets use the reference
drawing order to preserve transparency. Debug PNG output uses the same cache.

Use `--overlay_workers=1` to compare sequential rendering with the worker pool,
or choose a value up to 12. The default, `0`, selects automatically from the
available CPU threads and a memory allowance for frame buffers and caches.
The selected worker count and RGBA bytes per frame are printed at startup.
Rendering timers are summed elapsed durations across workers, not CPU usage;
they overlap FFmpeg processing and must not be added to the total elapsed time.
The detailed breakdown reports buffer/setup, track copy/arrow, and widget work.
Telemetry sampling and track snapshot preparation are reported separately.
Frame submission and FFmpeg finalization have separate wall-clock timers.

To benchmark overlay generation without FFmpeg, disk, or pipe throughput, build
and run `racevideo_overlay_benchmark`. It uses a deterministic 30 fps synthetic
route, 10 Hz navigation, changing G-force, and both speed units. Arguments are
width, height, frame count, worker count, and `reference` or `cached`:

```powershell
.\build\release\racevideo_overlay_benchmark.exe 1920 1080 900 1 reference
.\build\release\racevideo_overlay_benchmark.exe 1920 1080 900 1 cached
.\build\release\racevideo_overlay_benchmark.exe 1920 1080 900 4 cached
```

On Linux/macOS, use `./build/release/racevideo_overlay_benchmark` with the same
arguments. Use the same Release build revision and arguments on both machines;
repeat runs and compare medians. The reference mode performs the original
per-frame allocation, drawing, and track snapshot copy using the same ordered
worker queue. The sink samples one byte per memory page for a checksum; it does
not simulate the full FFmpeg transfer. Compare checksums as a quick consistency
check, not a substitute for the renderer's full pixel-comparison tests.
These results measure renderer throughput, not end-to-end video encoding speed.

RaceVideo also looks across the complete recording for stationary periods and
uses quiet accelerometer and gyroscope samples to correct small camera pitch
and roll mounting errors. It prints the applied angles when calibration is
reliable; otherwise it reports that only the orthogonal mount orientation is
being used.

### Sequential GoPro chapters

RaceVideo can treat several consecutive GoPro chapter files as one continuous
recording. Repeat `--input` in playback order:

```powershell
racevideo.exe --input="GX010001.MP4" --input="GX020001.MP4" `
  --input="GX030001.MP4" --imu_axis_order="ZXY" `
  --output_video="complete-drive.mp4" --speed_unit="kmh,mph"
```

The order of the `--input` arguments is preserved. A comma-separated value is
not supported because commas are valid filename characters.

For long lists, create a text file containing one video path per line, in
playback order:

```text
GX010001.MP4
GX020001.MP4
GX030001.MP4
```

Blank lines are ignored. Relative paths are resolved from the directory that
contains the list file. Pass the list instead of `--input`:

```powershell
racevideo.exe --input_list="chapters.txt" --imu_axis_order="ZXY" `
  --output_video="complete-drive.mp4" --speed_unit="kmh,mph"
```

RaceVideo validates that the chapters have matching video and audio stream
properties before rendering. It combines their telemetry and durations into a
single timeline, so `--start_seconds` and `--duration_seconds` apply to the
complete recording and the track represents the selected range across all
chapters. Initial scanning progress is printed for each chapter. Automatic
mount calibration uses stationary evidence from the entire set—including the
end of the final chapter—and applies one correction to every chapter. FFmpeg
reads the source chapters directly through its concat demuxer;
RaceVideo does not create an intermediate joined video. The input files remain
unchanged.

## Privacy

GoPro metadata can contain precise GPS locations, timestamps, device details,
and other sensitive information. Review extracted metadata before sharing it.

## License

RaceVideo is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
Third-party components retain their own licenses and notices; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Unless explicitly stated otherwise, contributions submitted for inclusion in
RaceVideo are provided under the Apache License 2.0.
