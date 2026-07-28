# Building 0.9.5-unified4

On 64-bit Windows, run:

```bat
BUILD_ALL.cmd
```

The scripts download verified portable Zig 0.14.1, CMake and Ninja tools as
needed. Git for Windows is used for the pinned Dynarmic source checkout. No
Visual Studio, WSL or administrator installation is required.

Output:

```text
dist-unified/
  x86/GeometryDashWrapper.exe
  arm-legacy/GeometryDashArmLegacy.exe
  armv7/GeometryDashArmV7.exe
  RUN_AUTO.cmd
  run_auto.py
  save/
```

Individual builds:

```bat
BUILD_X86.cmd
BUILD_DYNARMIC.cmd
```

The source package intentionally excludes APKs, extracted proprietary `.so`
files, executables, DLLs, downloaded toolchains and build caches.


## Launch settings

Edit `RUN_AUTO.cmd`. Unified4 adds `FORCE_HIGHEST_GRAPHICS=true`. An optional
`icon.png` may be placed beside the launcher or in `dist-unified`; otherwise
`run_auto.py` chooses a suitable icon from `game.apk`.
