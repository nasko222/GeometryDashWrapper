# Building Geometry Dash Wrapper 0.9.6-gdpsfixes6

Use `BUILD_ALL.cmd` from a Windows command prompt. The build scripts fetch/use
the project's pinned Zig 0.14.1, CMake, Ninja, Dynarmic and Boost dependencies
and produce the launcher plus x86, legacy ARM and ARMv7 backends.

The normal RUN batches set `I_LOST_THE_GAME=true`, `EDITOR_CONTROLLS=true`, and
`EXTRAS_MENU=true`. The executable-side defaults remain false.

This branch is Android-only. No APK, extracted game `.so`, built executable,
DLL, or proprietary game asset is included in the source archive.
