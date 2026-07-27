# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup12

This is the separate Dynarmic ARMv7 branch for the supplied Geometry Dash 2.2
beta APKs. The stable Dynarmic Test14-fix1 line for Geometry Dash 1.0–1.4 is
unchanged.

## Runtime design

- ARMv7 game code runs through Dynarmic on 64-bit Windows.
- The editor uses the targeted Bringup9 `LevelEditorLayerExt::initH` bridge.
- Whole companion `ApplyHooks`, DPAD, and collision hook groups stay disabled.
- Valid level setup strings from the game are never replaced.
- APK catalog data is decoded lazily only to recover an empty setup.
- A verified latest-inflate payload is the fallback for custom/editor levels.
- Windows platformer keys call `GJBaseGameLayer::queueButton` directly.

## Build

On 64-bit Windows:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-bringup12\
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup12\RUN_V22_SELECTED_APK.cmd
```

The source package contains no APK. A selected APK is copied only into the
generated local distribution directory.

## Controls

- Jump: mouse, Space, or Up Arrow
- Platformer left: A or Left Arrow
- Platformer right: D or Right Arrow

## Focused runtime test

1. Open the editor with the wrench-and-hammer button.
2. Place objects, playtest, pause, resume, and return.
3. Launch Power Trip, Knock Em Out, and Press Start.
4. Switch repeatedly between those levels and a normal official level.

Attach `gd-v22beta-selected.log` if a step fails.
