# Building Geometry Dash Wrapper 0.9.6-gdpstweaks13 on Windows

Use the normal project build entry point:

```bat
BUILD_ALL.cmd
```

Individual components can still be built with `BUILD_X86.cmd`, `BUILD_DYNARMIC.cmd`, and `BUILD_LAUNCHER.cmd`.

`gdpstweaks13` changes the x86 backend and ARMv7 editor/preview paths plus version/build metadata. The shared audio source is intentionally unchanged from tweaks12. **Rebuild all wrapper executables; do not reuse gdpstweaks10 binaries.**

The removed `REMOVE_PAUSE_BUTTON` and `HIDE_CURSOR_WHEN_PLAYING` environment variables remain absent.

No APK is included. Supply your own game APK at runtime through the existing launcher flow.
