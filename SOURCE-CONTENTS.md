# Source contents — DynarmicTest11

This is the complete source tree based on DynarmicTest10. No game source, APK, vendor dependency, tool, patch, previous diagnostic note, or build script was removed.

Test11-specific files:

- `CHANGELOG-0.9.4-arm-dynarmictest11.md`
- `DYNARMICTEST11-NOTES.md`

Core changed files:

- `src/dynarmic_probe.cpp` — correct zero-valued `SO_ERROR`, direct `CCApplication::openURL` hook, and bounded socket diagnostics.
- `dynarmic-x64/CMakeLists.txt` — Test11 project name; existing `shell32` and `ws2_32` linkage retained.
- `build-dynarmic-x64.ps1` — Test11 output folder and notes.
- `README.md`, `VERSION.txt`, and package verification metadata.
