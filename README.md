# Geometry Dash ARM Wrapper — Dynarmic x64 Test5

Current version: **0.9.4-arm-dynarmictest5**

Test5 keeps the persistent interactive Dynarmic wrapper from Test4 and adds guest-fatal diagnostics for the menu and level-loading aborts. The host executable is 64-bit Windows; the authentic game library remains a 32-bit ARMv5TE guest.

## Build

The full source package includes `game.apk` and the complete source/vendor tree. Run:

```cmd
BUILD_DYNARMIC_X64.cmd
```

The builder reuses `.build-tools` and `build-cache-windows` when available and creates:

```text
dist-arm-wrapper-dynarmictest5
```

Launch:

```text
dist-arm-wrapper-dynarmictest5\RUN_DYNARMIC_INTERACTIVE.cmd
```

## Controls

- Left mouse button: touch begin/end
- Mouse drag: touch move
- Space or Up Arrow: gameplay press/release
- Escape: Android Back key
- Text and Backspace: forwarded to the game
- Window deactivate/reactivate: Android pause/resume

## Test5 crash logging

When the ARM guest calls `abort`, `exit`, a stack-protector failure, or a long-jump fatal path, the log now records:

- the active native callback and nested guest-call chain
- symbolized PC and LR with `libgame.so` ELF offsets
- SP, CPSR, and registers R0-R12
- a 160-byte guest-stack window with candidate code addresses symbolized
- the most recent host input, JNI calls, runtime imports, and guest message box
- exactly one final fatal error line in the log

Input callbacks are also logged with coordinates, key codes, text, and guest entry addresses.

## Source-tree policy

This package preserves all functional source and dependencies from the corrected Test4 full-source package, including `game.apk`, the existing Unicorn wrapper source, vendor source, patches, build scripts, and toolchain helpers. Only the superseded Test4 notes/changelog are replaced by current Test5 documentation. No destructive cleanup script is included.

See `DYNARMICTEST5-NOTES.md` for the diagnostic format and `SOURCE-CONTENTS.md` for packaging checks.
