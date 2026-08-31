# RaceVideo TODO

## Interactive sensor visualization

Create an optional `racevideo-viewer` application for synchronized video and
telemetry exploration. It should combine accelerometer/gyroscope waveforms,
spectrograms, RMS/peak envelopes, GPS, and an interactive 3D camera or vehicle
model with vector arrows and short trails.

Reuse simplified views as future video overlays, including a G-ball, vector
trail, vibration meter, three-axis strip chart, and impact markers. Keep the
viewer separate from the command-line extractor while sharing `.rvt`, signal
processing, filtering, and synchronization code.

See `RaceVideo_Design_Plan.md`, "Future Telemetry Visualization," for the
detailed design and implementation sequence.

## Deferred orientation calibration

The initial implementation assumes the camera points forward and corrects only
upright, upside-down, 90-degree-left, and 90-degree-right mounting. Defer
fine-angle tilt correction, GPS-derived mounting alignment, optical horizon or
vanishing-point analysis, and full sensor fusion until a demonstrated use case
requires them.
