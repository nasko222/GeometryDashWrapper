# Geometry Dash 2.2 beta ARMv7 Bringup14

## Evidence from logs 17 and 18

- Log 17 reaches editor creation and the targeted companion `initH` bridge,
  then stops immediately after entering the editor. The companion visibility
  helper scans all 10,000 editor section arrays in emulated ARM every frame.
- Log 18 successfully inflates the test platformer level, then reaches
  `GDPSManager::detectEmulators()` in companion `libgame.so` and terminates
  through `__stack_chk_fail`.
- Neither failure is in the level-data decoder or Bringup13's normal gameplay
  path.

## Changes

- `LevelEditorLayer::updateVisibility(float)` now enters a host bridge.
  CCArray section storage is scanned natively, with at most 32 newly discovered
  objects activated per frame. Each object is attached, activated, made
  visible, assigned full opacity, and color/sort queues are finalized through
  primary `libcocos2dcpp.so` functions.
- `PlayLayer::updateVisibility(float)` runs its untouched primary function
  first through a guest thunk. A host post-pass applies full opacity only to
  the platformer control sprite and direct child tag 2990.
- Companion `GDPSManager`, emulator detection, full `ApplyHooks`,
  constructors, DPAD touch hooks, collision hooks, and other gameplay hooks
  are not executed.

## Preserved behavior

- Bringup13's normal-level, large-string, empty-only recovery, editor `initH`,
  direct platformer keyboard input, MCI music, and waveOut sound-effect paths
  remain unchanged.
- The stable Dynarmic Test14-fix1 line for Geometry Dash 1.0–1.4 is untouched.
- No APK or extracted proprietary game library is included in release
  artifacts.

## Test priority

1. Replay ordinary levels to confirm the Bringup13 baseline.
2. Open the editor, place objects, scroll, playtest, and return.
3. Load the same platformer test level that failed in log 18.
4. Verify overlay visibility and A/D/Space input.
5. Retry Power Trip, Knock Em Out, and Press Start.
