# Geometry Dash 2.2 beta ARMv7 Bringup15

## Evidence

- Bringup14 log 19 records A/D reaching the host and direct
  `GJBaseGameLayer::queueButton` calls, while an editor playtest ignores
  movement. The editor's live input owner is `EditorUI`, not the stale
  `GameManager` gameplay-layer pointer.
- `UILayer::ccTouchBegan` marks a platformer canvas touch separately from its
  left/right controls. This provides native mouse-jump exclusion without
  hard-coded screen coordinates.
- The early 2019 beta has primary offsets `0x284/0x104` and no companion
  editor library. A separate beta has a different companion ABI, while the
  167 MB beta's companion lacks `LevelEditorLayerExt::initH`. Bringup14 treated
  that optional symbol as mandatory and exited before gameplay.
- The early beta's official-level failure occurred only when the level setup
  string was empty and wrapper recovery had no matching payload.

## Changes

- Platformer keyboard input now enters native `UILayer::keyDown/keyUp`.
  Editor playtests use `EditorUI::keyDown/keyUp`. This fixes movement and lets
  the game update its own visible button state.
- A native canvas touch marker adds mouse-left jump after the game's touch
  routing has excluded pause and platformer controls. Space and Up share the
  same native jump path.
- F2 again invokes `CreatorLayer::onMyLevels`.
- Companion mapping is capability-gated. Only the validated selected-beta
  companion CRC `0x90ac09a5` with the expected late layout may run targeted
  `initH` and visibility bridges. Missing or unknown helpers no longer abort
  the APK and are never called just because a similarly named symbol exists.
- Early-layout `0x284/0x104` APKs receive the native editor bridge without a
  companion.
- Level recovery validates the `GJGameLevel` vtable, scans compatible
  PlayLayer fields, and can map official music filenames to level IDs. If no
  recovery payload exists, a valid guest string is passed to native code; an
  invalid empty COW string receives a parseable empty setup.

## Safety boundary

- Companion constructors, complete `ApplyHooks`, GDPS/emulator hooks, DPAD
  touch hooks, collision hooks, and broad gameplay hooks remain disabled.
- Bringup14's stable selected-beta levels, native visibility bridge, MCI music,
  and waveOut effects are preserved.
- Dynarmic Test14-fix1 for Geometry Dash 1.0–1.4 is untouched.
- No APK or extracted proprietary native game library is included.

## Test priority

1. Run a normal selected-beta level as a regression check.
2. Test mouse-left, Up, Space, A/D, and arrows in platformer gameplay.
3. Confirm keyboard left/right visually depress their native controls.
4. Test A/D and arrows in a platformer editor playtest.
5. Press F2 from a menu to enter My Levels.
6. Test an official level and the editor in the early 2019 beta.
