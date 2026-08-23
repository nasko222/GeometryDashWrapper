# Building Geometry Dash Wrapper 0.9.6-gdpstweaks10 on Windows

Use the normal project build entry point:

```bat
BUILD_ALL.cmd
```

Individual components can still be built with `BUILD_X86.cmd`, `BUILD_DYNARMIC.cmd`, and `BUILD_LAUNCHER.cmd`.

`gdpstweaks10` changes x86/legacy/ARMv7 desktop input/UI code, ARMv7 editor restoration, shared runtime settings, launcher metadata and Dynarmic build metadata. Rebuild all wrapper executables; do not reuse tweaks9 binaries.

The removed `REMOVE_PAUSE_BUTTON` and `HIDE_CURSOR_WHEN_PLAYING` environment variables are no longer settings in this branch.

No APK is included. Supply your own game APK at runtime through the existing launcher flow.
