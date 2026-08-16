# Building PublicTest2

Run `BUILD_ALL.cmd` on Windows. The scripts fetch or reuse the pinned CMake,
Ninja, Zig and Dynarmic dependencies and build the native launcher plus four
runtime backends:

- x86 Android
- legacy ARM Android
- ARMv7 Android
- ARMv7 iOS bootstrap

The iOS backend uses the same Dynarmic A32 core and bundled zlib source. No
Python or Apple SDK/toolchain is required to build the wrapper.

No APK, IPA, extracted Android/iOS executable, built EXE/DLL, Python file or
`.gitignore` is included in this source archive.
