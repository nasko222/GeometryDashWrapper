# Geometry Dash 2.2 beta ARMv7 Bringup12

Bringup12 is a recovery build based on direct runtime evidence from Bringup11.

## Editor recovery

Bringup11 installed six companion `LevelEditorLayerExt` and
`EditorPauseLayerExt` hooks before opening the editor. The failure log then
spent more than 500 million guest ticks in a C++ RTTI `__do_upcast` loop and
never returned from `LevelEditorLayer::create`.

Bringup12 removes that hook installation path. Editor entry again uses the
narrow Bringup9 sequence:

1. Create `LevelEditorLayer`.
2. Call only the companion `LevelEditorLayerExt::initH` bridge.
3. Put the initialized layer in a scene.

Companion constructors, complete `ApplyHooks` groups, DPAD hooks, and collision
hooks are not run.

## Level recovery

Bringup11 treated the APK catalog as authoritative and replaced any valid guest
level string that differed from it. Bringup12 preserves every valid, non-empty
setup supplied by the game.

The per-level APK catalog and latest verified inflate payload are now used only
when the guest setup is empty or structurally unreadable. This retains recovery
for the beta's empty-level failure without rewriting levels that already worked
in Bringup9.

The stable Dynarmic Test14-fix1 source for Geometry Dash 1.0–1.4 is unchanged.
