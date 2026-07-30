# EnduranceTest8

EnduranceTest8 is a narrow correction based on the 2026-07-30 Windows logs and
screenshot. It does not change x86 pacing, checkpoints, networking, saves or the
stable editor object/selection work from EnduranceTest6/7.

## Selected 2.2 beta editor

- Patches only the `LevelEditorLayer::updateEditor` call to
  `GJBaseGameLayer::updateCameraBGArt`. The same function remains untouched in
  PlayLayer/gameplay. This freezes the editor background art instead of letting
  the black wrapped region move across the window and restart at the left edge.
- Rebuilds BPM markers through the game's real
  `LevelEditorLayer::levelSettingsUpdated()` path once per editor session.
  Direct `DrawGridLayer::updateTimeMarkers()` was insufficient because it only
  reparses an already-populated marker string, which was empty in the failing
  run.
- Guards only the audited `CCSpriteBatchNode::initWithTexture` call to
  `updateBlendFunc()`. A null `this` at this exact call caused the repeatable
  ground-texture crash in the uploaded log. Non-null calls execute the original
  guest function.

Expected diagnostics:

```text
DYNARMIC_V22_EDITOR_BACKGROUND_FROZEN
DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH mode=level-settings-updated
DYNARMIC_V22_NULL_BATCH_BLEND_GUARD
```

## Legacy audio experiment

Every previous volume-maintenance/reassert experiment remains absent. Legacy ARM
now enables one opposite experiment: the first play of a newly-opened,
non-looping level track is set to MCI volume 1000, aiming to make attempt 1 as
loud as attempt 2+. Menu loops and non-legacy backends are unchanged.

## Not claimed runtime-confirmed

The editor background freeze, BPM lines, ground texture crash guard and legacy
attempt-1 loudness require a real Windows/APK test. The isolated Geometry Dash
1.3 freeze remains open because the uploaded logs contain no fault or stable
reproduction point.
