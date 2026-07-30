# Building EnduranceTest11

Run `BUILD_ALL.cmd` on Windows. The scripts fetch or reuse the pinned CMake,
Ninja, Zig and Dynarmic dependencies and build the native launcher plus all
wrapper backends.

No APK, extracted Android library, built EXE/DLL, Python file or `.gitignore` is
included in this source archive. Supply APKs through the normal launcher flow.

Runtime changes are limited to:

- the ARMv7 2.2-beta null-ground fallback path; and
- a legacy-ARM-only first-play MCI decoder prime.

The complete x86 backend is unchanged. The confirmed editor-background fix,
BPM path, selection rendering, and Practice Z/X implementation are unchanged.
