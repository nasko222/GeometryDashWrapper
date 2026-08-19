# Building Geometry Dash Wrapper 0.9.6-gdpsfixes3

Use `BUILD_ALL.cmd` from a Windows command prompt. The build scripts fetch/use the
project's pinned Zig, CMake, Ninja, Dynarmic and Boost dependencies and produce:

- the native launcher;
- x86-native backend files;
- `arm-legacy/GeometryDashArmLegacy.exe`;
- `armv7/GeometryDashArmV7.exe`.

This branch is Android-only.

No APK, extracted game `.so`, built executable, or proprietary game asset is
included in the source archive.
