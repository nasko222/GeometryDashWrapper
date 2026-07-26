# Source contents — DynarmicTest10

This is the complete source tree based on DynarmicTest9 Fix1. No game source, APK, vendor dependency, tool, patch, or build script was removed.

Test10-specific files:

- `CHANGELOG-0.9.4-arm-dynarmictest10.md`
- `DYNARMICTEST10-NOTES.md`

Core changed files:

- `src/dynarmic_probe.cpp` — completed TCP connections and Windows browser JNI bridge.
- `dynarmic-x64/CMakeLists.txt` — Test10 project name and `shell32` linkage.
- `build-dynarmic-x64.ps1` — Test10 output folder and notes.
- `README.md`, `VERSION.txt`, and package verification metadata.
