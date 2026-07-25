# Geometry Dash ARM Wrapper — Dynarmic x64 Test6

Current version: **0.9.4-arm-dynarmictest6**

Test6 fixes the deterministic `std::bad_alloc` reached when opening Stereo Madness or another level. The Test5 crash dump proved that the old 128 MiB bump-only guest heap had reached its final few kilobytes.

## Build

Run:

```cmd
BUILD_DYNARMIC_X64.cmd
```

The builder creates:

```text
dist-arm-wrapper-dynarmictest6
```

Launch:

```text
dist-arm-wrapper-dynarmictest6\RUN_DYNARMIC_INTERACTIVE.cmd
```

## Test6 allocator repair

- imported `free()` now releases guest allocations
- `realloc()` can shrink, grow in place, or move and release the old block
- `mmap()` allocations are page aligned and `munmap()` reclaims them
- free blocks are split, coalesced, reused with best-fit selection, and returned from the heap top
- the qsort bridge uses one reusable scratch block instead of allocating inside every comparison
- mapped heap headroom is increased from 128 MiB to 256 MiB
- allocation failure and fatal dumps include arena, live, free, peak, and call statistics

## Controls

- Left mouse button: touch begin/end
- Mouse drag: touch move
- Space or Up Arrow: gameplay press/release
- Escape: Android Back key
- Text and Backspace: forwarded to the game
- Window deactivate/reactivate: Android pause/resume

## Diagnostics retained from Test5

Fatal guest calls still record the active callback, symbolized PC/LR, registers, stack window, recent input/JNI/import events, assertion text, and now heap state.

## Source-tree policy

This is based on the corrected 33.9 MB DynarmicTest4 full-source package. It preserves `game.apk`, the Unicorn wrapper, vendor sources, build scripts, patches, licenses, and toolchain helpers. No cleanup script removes project dependencies.

See `DYNARMICTEST6-NOTES.md` and `SOURCE-CONTENTS.md`.
