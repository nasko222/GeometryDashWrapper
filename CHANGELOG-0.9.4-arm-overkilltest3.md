# Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest3

Based on OverkillTest2.

- Adds an automatic slow-path ARM basic-block profiler.
- Starts a five-second F11-equivalent sample after 10 consecutive nativeRender calls of 22-100 ms with no APK opens, file reads, or zlib work.
- Captures up to three samples with a ten-second cooldown.
- Records per-render APK/file/zlib deltas so loading stalls cannot trigger the gameplay profiler.
- Adds `RUN_AUTO_PROFILE_ORIGINAL.cmd` to reproduce lag with original audio, textures, particles, and drawing enabled.
- Keeps F11 manual profiling and every OverkillTest2 toggle.
