# Geometry Dash ARM Wrapper — Dynarmic x64 Test7

Current version: **0.9.4-arm-dynarmictest7**

Test7 continues the interactive ARMv5TE-on-x64 Dynarmic wrapper. It fixes persistent save crashes caused by unmapped fake `FILE*` objects and enables the Windows audio bridge.

## Build

Run on 64-bit Windows:

```cmd
BUILD_DYNARMIC_X64.cmd
```

The builder reuses its downloaded tools and build cache, then creates:

```text
dist-arm-wrapper-dynarmictest7
```

Launch:

```text
dist-arm-wrapper-dynarmictest7\RUN_DYNARMIC_INTERACTIVE.cmd
```

The full source package includes `game.apk`, the complete source/vendor tree, build scripts, patches and licenses.

## Controls

- Left mouse button: touch begin/end
- Mouse drag: touch move
- Space or Up Arrow: gameplay press/release
- Escape: Android Back
- Text and Backspace: editor text input
- Window deactivate/reactivate: Android pause/resume

## Test7 focus

- mapped Bionic-compatible guest file objects
- stable `CCGameManager.dat` and `CCLocalLevels.dat` close/save paths
- background music and sound effects through Windows MCI
- APK music extraction and OGG-to-WAV effect cache
- retained reclaiming allocator and symbolized fatal diagnostics

See `DYNARMICTEST7-NOTES.md` for technical details.
