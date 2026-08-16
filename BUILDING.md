# Building PublicTest1

Run `BUILD_ALL.cmd` on Windows. The scripts fetch or reuse the pinned CMake,
Ninja, Zig and Dynarmic dependencies and build the native launcher plus all
Android wrapper backends.

PublicTest1 adds the IPA analyzer to the native launcher only. The analyzer uses
the launcher's existing zlib-backed ZIP reader and does not add a Python or
Apple-tool dependency.

No APK, IPA, extracted Android/iOS executable, built EXE/DLL, Python file or
`.gitignore` is included in this source archive.
