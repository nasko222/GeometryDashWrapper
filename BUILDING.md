# Building Geometry Dash Wrapper 0.9.7-cof3 on Windows

Use a fresh extracted COF3 source directory on 64-bit Windows and run:

```bat
BUILD_ALL.cmd
```

This builds the x86 backend, legacy ARM Dynarmic backend, ARMv7 Dynarmic
backend, and native launcher into:

```text
dist-unified\
```

The build scripts fetch their pinned public toolchain/dependencies as needed.
The ARM builder revision for this branch is
`dynarmic-x64-builder106-0.9.7-cof3`.

Both normal run scripts set `FPS=VSYNC`. Edit that line to a numeric value such
as `144`, `240`, or `9999` to disable VSync and use the host FPS cap instead.

After building, drag the desired APK onto `RUN_AUTO_GDPS.cmd` or
`RUN_AUTO_BOOMLINGS.cmd`.
