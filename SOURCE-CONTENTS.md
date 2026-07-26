# Source contents — DynarmicTest13

This is the complete source tree based on DynarmicTest12. No APK, source file, dependency, tool, patch, build script, or previous diagnostic/changelog file was removed.

Test13-specific files:

- `CHANGELOG-0.9.4-arm-dynarmictest13.md`
- `DYNARMICTEST13-NOTES.md`

Core changed files:

- `src/dynarmic_probe.cpp` — guest page lookup, typed memory callbacks, cached OpenGL dispatch, buffered diagnostics, host/import sampling, GPU query timing, per-frame profiler, CSV/summary output, and Test13 labels.
- `dynarmic-x64/CMakeLists.txt` — Test13 project name and `psapi` linkage for process-memory diagnostics.
- `build-dynarmic-x64.ps1` — Test13 output directory, notes, debug-everything launcher, and profiler-file guidance.
- `README.md`, `VERSION.txt`, and `PACKAGE-VERIFICATION.txt` — Test13 metadata.
