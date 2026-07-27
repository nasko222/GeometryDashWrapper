# Geometry Dash 2.2 beta ARMv7 Bringup13

Bringup13 keeps Bringup12's stable editor-entry and non-destructive level-data
paths, then addresses the four failures observed in logs 14–16.

## Large levels: Press Start and Knock Em Out

The wrapper treated one mebibyte as the maximum guest C-string length. That
limit is smaller than both failing decoded level setups:

- Press Start: 1,241,094 bytes
- Knock Em Out: 1,091,308 bytes
- Power Trip, which loaded: 889,245 bytes

The resulting truncation explains why both failures occurred while the game was
turning the decoded setup into C++ strings/objects. Guest C-string operations
now permit up to 64 MiB while remaining bounded by the mapped guest region.
This is a safety ceiling, not an allocation made for every string.

Valid, non-empty level data supplied by the game is still preserved. The APK
catalog and latest verified inflate payload remain empty/unreadable-data
fallbacks only.

## Editor and platformer visuals

The complete companion `ApplyHooks` groups remain disabled. They previously
caused editor RTTI runaway and global gameplay regressions.

Bringup13 redirects only two visibility methods:

1. `LevelEditorLayer::updateVisibility(float)` to the companion editor helper.
2. `PlayLayer::updateVisibility(float)` to the companion platformer-UI helper.

Static audit finds exactly one vtable pointer and no direct call sites for each
redirect. The editor still enters through the proven targeted
`LevelEditorLayerExt::initH` path. The companion editor-init, opacity, DPAD
touch, editor-touch, and collision hooks are not installed.

## Platformer jump

Space and Up Arrow now queue platformer button 1 directly through
`GJBaseGameLayer::queueButton`, matching the already-working mappings:

- jump: button 1
- left: button 2
- right: button 3

Mouse/touch delivery remains native.

## Sound effects

Music remains on the working MCI path. WAV sound effects no longer use MCI,
which returned error 304 for every attempted effect in the supplied logs.
Effects now use `waveOut` with 48 overlapping playback slots, per-effect IDs,
looping, pause/resume, stop, and volume handling.

## Preserved boundaries

- Stable Dynarmic Test14-fix1 for Geometry Dash 1.0–1.4 is unchanged.
- The complete companion hook sets and constructors are not run.
- No APK or extracted proprietary library is included.
- Static and cross-build checks cannot replace a Windows gameplay test.
