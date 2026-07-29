# Building EnduranceTest1 on Windows

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

EnduranceTest1 changes x86 networking, x86/ARM cursor behavior, ARMv7 editor
behavior and the native launch metadata. Run `BUILD_ALL.cmd`; do not reuse Fix6
executables. The x86 pacing implementation itself is unchanged.
