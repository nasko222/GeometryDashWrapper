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
