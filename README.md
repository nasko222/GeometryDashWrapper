# Geometry Dash Wrapper 0.9.7-cof5

`cof5` means **Cleanup Optimization Fixes 5**.

COF5 is the clean COF2 gameplay/editor baseline plus configurable frame pacing.
The experimental Geometry Dash 1.02 comments hotkey from COF3/COF4 has been
removed completely.

## ARMv7 2.2 policy

- The modded/selected 2023 beta uses the old Endurance/Bringup companion path:
  `LevelEditorLayer::create` -> validated `LevelEditorLayerExt::initH` from the
  APK's own compatible `libgame.so` -> normal Cocos scene.
- EnduranceTest10 editor visibility, song-guide/BPM overlay, background safety,
  platformer input and editor-entry behavior remain.
- Stock 2019/2022/2023 editor stubs are not reconstructed by the wrapper.

## FPS

The launch scripts default to:

```bat
set "FPS=VSYNC"
```

Use `FPS=144`, `FPS=240`, `FPS=9999`, or another numeric value from 1 through
10000 to disable VSync and use the host frame limiter. Invalid values fall back
to VSYNC.

## 1.02 comments

COF5 contains no comments hotkey or comments-specific wrapper path. Pressing C
is not intercepted for Geometry Dash 1.02.

## Build

On 64-bit Windows run:

```bat
BUILD_ALL.cmd
```

Output is written to `dist-unified\`.

See `COF1.md`, `COF2.md`, and `COF5.md` for branch history and details.
