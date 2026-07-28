# Building on Windows

Run:

```bat
BUILD_ALL.cmd
```

The build downloads pinned portable Zig, CMake and Ninja tools as needed. Python
is not required for building or running this release.

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
  save\
  logs\
```

Individual builders:

```bat
BUILD_X86.cmd
BUILD_DYNARMIC.cmd
BUILD_LAUNCHER.cmd
```

The ARM/Dynarmic source changed in Fix4, so do not reuse an old `armv7` or
`arm-legacy` executable. `build_dynarmic.ps1` has a new builder revision and
will reject an incompatible cached backend.
