# Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest2

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
- F11 automatic five-second ARM basic-block profiler with nearest ELF symbol
  mapping.
- Dedicated bare-minimum, logic-only, zero-render and control launchers.
- Expanded overkill profile counters and active-state flags.

## Corrected diagnostics

- Runtime F4/F5/F7 code patches now temporarily bypass the immutable-image host
  corruption guard for their verified four-byte target, then restore the guard
  and invalidate only the patched translation range.
- This fixes the observed `UC_ERR_WRITE_PROT` failures where F4 reported
  `applied=0 failed=7` and F7 never disabled `CCNode::visit`.
- Added automatic cache regeneration when the F11 block profiler is enabled or
  removed.
- Function-key auto-repeat is ignored, preventing one held key from toggling a
  diagnostic off and immediately back on.

## Retained

- PerformanceTest1's indexed APK lookup, translation cache, direct guest-memory
  OpenGL uploads and fast import classifications.
- Bootstrap15's memory-integrity, immutable-image, save-transaction, allocator,
  label and corruption protections.

## Intentional limitations

This build may display white, missing or blank graphics. That is expected. It is
for locating the bottleneck, not for normal play or visual correctness.
