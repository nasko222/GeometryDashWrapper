# Geometry Dash 2.2 beta ARMv7 bringup4

The Bringup3 logs proved that constructors, JNI_OnLoad, OpenGL, the first frame, menus, input, and sustained 60 FPS all work. Bringup4 addresses the next three runtime boundaries.

## Refresh-rate bridge

`BaseRobTopActivity.getDeviceRefreshRate()F` previously fell through the generic JNI float dispatcher and returned `0.0f`. Newer Geometry Dash uses this value when establishing its simulation cadence. Bringup4 returns the wrapper's current 60 Hz target and logs the bridge once. This directly targets the editor playtest that rendered one frame without advancing the cube.

## FMOD bridge

The first ARMv7 bridge started a background MCI channel immediately and then paused it when FMOD requested a paused channel. That does not match FMOD's deferred-start behavior and can produce a successful "playing" log with no audible menu music. Bringup4 ports the proven state model from the x86 compatibility bridge:

- FMOD version 1.05.04;
- 44.1 kHz stereo software format;
- background classification for streams and MP3 files;
- deferred music start until `setPaused(false)`;
- pause position capture and exact resume;
- position, volume, loop-mode, mixer suspend/resume, DSP metering, and stream-buffer state;
- detailed logging for the first 96 guest FMOD calls.

## Level setup guard

Both tested official levels fail at the same instruction after `CCArray::stringAtIndex(0)` returns null. The four-byte `CCString::getCString()` accessor reads null + `0x30`; by the time the callback stops Dynarmic, execution has returned to `PlayLayer::prepareCreateObjectsFromSetup` at symbol entry + `0x148`.

Bringup4 recognizes only that exact symbol-relative fault and substitutes a persistent empty C string, allowing `LevelSettingsObject::objectFromString` to create default settings. Other invalid-memory accesses still fail with expanded R0-R3/SP diagnostics.

No APKs are included in this archive.
