# Building EnduranceTest4 on Windows

Run:

```bat
BUILD_ALL.cmd
```

The build downloads pinned portable Zig, CMake and Ninja tools as needed.
Python is not required.

Output:

```text
dist-unified\
  GeometryDashLauncher.exe
  RUN_AUTO_GDPS.cmd
  RUN_AUTO_BOOMLINGS.cmd
  x86\GeometryDashWrapper.exe
  arm-legacy\GeometryDashArmLegacy.exe
  armv7\GeometryDashArmV7.exe
  assets\icons\...
  save\<package>__v<version>__<backend>\
  logs\YYYY-MM-DD\...
```

Individual builders:

```bat
BUILD_X86.cmd
BUILD_DYNARMIC.cmd
BUILD_LAUNCHER.cmd
```

EnduranceTest4 changes input and desktop state on every backend, shared audio,
ARMv7 editor rendering, and build metadata. Run `BUILD_ALL.cmd`; do not reuse
EnduranceTest3 executables.
