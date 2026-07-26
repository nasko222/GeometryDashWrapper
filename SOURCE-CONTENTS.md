# Source contents — DynarmicTest12

This is the complete source tree based on DynarmicTest11. No game APK, source file, vendor dependency, tool, patch, previous diagnostic note, or build script was removed.

Test12-specific files:

- `CHANGELOG-0.9.4-arm-dynarmictest12.md`
- `DYNARMICTEST12-NOTES.md`

Core changed files:

- `src/dynarmic_probe.cpp` — select-based guest poll, bounded nonblocking send/receive waits, diagnostics, and Test12 labels.
- `dynarmic-x64/CMakeLists.txt` — Test12 project name; existing `shell32` and `ws2_32` linkage retained.
- `build-dynarmic-x64.ps1` — Test12 output folder and notes.
- `README.md`, `VERSION.txt`, and package verification metadata.
