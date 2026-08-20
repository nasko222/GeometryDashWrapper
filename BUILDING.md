# Building Geometry Dash Wrapper 0.9.6-gdpstweaks7

Use `BUILD_ALL.cmd` from a Windows command prompt. The build scripts fetch/use
the project's pinned Zig 0.14.1, CMake, Ninja, Dynarmic and Boost dependencies
and produce the launcher plus x86, legacy ARM and ARMv7 backends.

The normal RUN batches set `I_LOST_THE_GAME=true` and `EDITOR_CONTROLLS=true`.
`REMOVE_PAUSE_BUTTON` and `HIDE_CURSOR_WHEN_PLAYING` default to true in tweaks7 and remain individually toggleable with the launch environment.
Extras is temporarily disabled.

This branch is Android-only. No APK, extracted game `.so`, built executable,
DLL, or proprietary game asset is included in the source archive.
