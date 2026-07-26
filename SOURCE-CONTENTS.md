# Source contents — DynarmicTest14

This is the complete DynarmicTest13 source plus the Test14 direct asynchronous effect and text-input-prewarm changes. No APK, dependency, tool, prior changelog, prior notes, or build material was removed.

Test14-specific files:

- `CHANGELOG-0.9.4-arm-dynarmictest14.md`
- `DYNARMICTEST14-NOTES.md`

Primary changed files:

- `src/audio_win.c` — ordered asynchronous effect command worker and cached decoder-slot operations.
- `src/audio_win.h` — parameter-preserving `audio_play_effect_ex` interface.
- `src/dynarmic_probe.cpp` — direct ARM effect hooks, stack-argument decoding, text-input asset prewarm, and Test14 markers.
- `dynarmic-x64/CMakeLists.txt` — Test14 project name.
- `build-dynarmic-x64.ps1` — Test14 output folder, builder revision, notes, and messages.
- `README.md`, `VERSION.txt`, and `PACKAGE-VERIFICATION.txt` — Test14 metadata.
