# Geometry Dash ARM Wrapper — Dynarmic x64 Test4

Current version: **0.9.4-arm-dynarmictest4**

Test4 turns the proven Dynarmic x64 first-frame build into a persistent interactive wrapper. The host executable is 64-bit Windows; the authentic game library remains a 32-bit ARMv5TE guest.

## Build

The full source package already includes `game.apk` and the complete wrapper/vendor source tree. Run:

```cmd
BUILD_DYNARMIC_X64.cmd
```

The builder reuses `.build-tools` and `build-cache-windows` when they already exist and creates:

```text
dist-arm-wrapper-dynarmictest4
```

Launch:

```text
dist-arm-wrapper-dynarmictest4\RUN_DYNARMIC_INTERACTIVE.cmd
```

## Controls

- Left mouse button: touch begin/end
- Mouse drag: touch move
- Space or Up Arrow: gameplay press/release
- Escape: Android Back key
- Text and Backspace: forwarded to the game
- Window deactivate/reactivate: Android pause/resume

## Source-tree policy

This package keeps all functional source and build dependencies from the working wrapper tree, including the existing Unicorn wrapper source, vendor source, patches, build scripts, toolchain helpers, and `game.apk`. Only obsolete milestone notes/changelogs were removed. No cleanup script is included.

See `DYNARMICTEST4-NOTES.md` for runtime details and `SOURCE-CONTENTS.md` for the preservation checks used when packaging.
