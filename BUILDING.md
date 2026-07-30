# Building EnduranceTest10

Run `BUILD_ALL.cmd` on Windows. The scripts fetch or reuse the pinned CMake,
Ninja, Zig and Dynarmic dependencies and build the native launcher plus all
wrapper backends.

No APK, extracted Android library, built EXE/DLL, Python file or `.gitignore` is
included in this source archive. Supply APKs through the normal launcher flow.

The EnduranceTest10 changes are confined to the ARMv7 2.2-beta editor path and
version metadata. The x86 backend is unchanged.
