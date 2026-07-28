# Building on Windows

Run:

```bat
BUILD_ALL.cmd
```

The build downloads pinned portable Zig, CMake and Ninja tools as needed.
Python is not required for building or running this release.

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

All three backends and the launcher contain Fix5 changes. Run `BUILD_ALL.cmd`;
do not reuse Fix4 executables. The Dynarmic builder revision was incremented so
stale ARM outputs are rebuilt.
