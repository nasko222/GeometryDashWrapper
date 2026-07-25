# Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest1

Destructive subsystem-isolation build based on PerformanceTest1.

## Added

- Complete `--no-audio` initialization bypass and safe JNI audio stubs.
- 70-byte 1x1 PNG substitution before guest image decoding.
- `--solid-textures`, `--no-textures` and fake `--headless-gl` modes.
- Guest Thumb no-op patches for particle, trail and cosmetic functions.
- Guest Thumb no-op patches for common sprite, batch, atlas, layer, label and
  shape draw methods.
- `--skip-draws`, `--skip-scene-visit` and `--skip-native-render` diagnostics.
- F3-F10 live subsystem toggles.
- Dedicated bare-minimum, logic-only, zero-render and control launchers.
- Expanded overkill profile counters and active-state flags.

## Retained

- PerformanceTest1's indexed APK lookup, translation cache, direct guest-memory
  OpenGL uploads and fast import classifications.
- Bootstrap15's memory-integrity, immutable-image, save-transaction, allocator,
  label and corruption protections.

## Intentional limitations

This build may display white, missing or blank graphics. That is expected. It is
for locating the bottleneck, not for normal play or visual correctness.
