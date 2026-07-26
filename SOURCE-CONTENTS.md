# Source contents — v22beta-bringup1

This full-source package preserves all 1,134 files from DynarmicTest14 and adds the separate ARMv7 beta branch files:

- `libcocos2dcpp.so` — supplied ARMv7 beta native library.
- `V22BETA-BRINGUP1-NOTES.md` — usage and technical status.
- `CHANGELOG-0.9.4-arm-v22beta-bringup1.md`.
- `V22BETA-STATIC-AUDIT.txt`.
- `V22BETA-IMPORT-MANIFEST.txt`.
- `BUILD_V22BETA_X64.cmd`.

Modified branch files:

- `src/dynarmic_probe.cpp`.
- `build-dynarmic-x64.ps1`.
- `dynarmic-x64/CMakeLists.txt`.
- `README.md`.
- `VERSION.txt`.
- `PACKAGE-VERIFICATION.txt`.

The original Test14 `game.apk`, history, third-party sources, build helpers and earlier notes remain preserved. The v22 builder defaults to `libcocos2dcpp.so`, so the retained old APK is not accidentally used for this branch.
