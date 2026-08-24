# Building Geometry Dash Wrapper 0.9.6-gdpstweaks12 on Windows

Use the normal project build entry point:

```bat
BUILD_ALL.cmd
```

Individual components can still be built with `BUILD_X86.cmd`, `BUILD_DYNARMIC.cmd`, and `BUILD_LAUNCHER.cmd`.

`gdpstweaks12` changes the x86 backend, ARMv7 editor/input/visual paths, the shared Windows audio backend, and version/build metadata. **Rebuild all wrapper executables; do not reuse gdpstweaks10 binaries.**

The removed `REMOVE_PAUSE_BUTTON` and `HIDE_CURSOR_WHEN_PLAYING` environment variables remain absent.

No APK is included. Supply your own game APK at runtime through the existing launcher flow.
