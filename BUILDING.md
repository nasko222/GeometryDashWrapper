# Building Unified7 Recovery

On 64-bit Windows, run:

```bat
BUILD_ALL.cmd
```

The build downloads or reuses the pinned Zig, CMake, Ninja, Boost and Dynarmic
tools, then creates `dist-unified\` with:

- `RUN_AUTO.cmd` and `run_auto.py`
- `x86\GeometryDashWrapper.exe`
- `arm-legacy\GeometryDashArmLegacy.exe`
- `armv7\GeometryDashArmV7.exe`
- `assets\icons\` with the real game icon resources
- one flat `save\` directory

Copy a supported APK to `dist-unified\game.apk`, edit `RUN_AUTO.cmd`, and run it.

## Logging-only launcher

`RUN_AUTO.cmd` accepts a dropped APK path. The launcher creates one unique run
directory under `dist-unified\logs` and stores all diagnostic output there.
No backend rebuild is required when applying only `RUN_AUTO.cmd` and `run_auto.py`
to an existing Fix2 distribution.
