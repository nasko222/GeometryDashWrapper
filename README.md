# Geometry Dash ARM Wrapper — Dynarmic x64 Test8

Current version: **0.9.4-arm-dynarmictest8**

Test8 keeps Test7's smooth gameplay, stable saves and Windows audio, then targets the remaining wrapper overhead: repeated APK ZIP reads during level/menu loading, the guest Exit button not closing the host window, and Space triggering gameplay while a text field is active.

## Build

Run on 64-bit Windows:

```cmd
BUILD_DYNARMIC_X64.cmd
```

The builder reuses its downloaded tools and build cache, then creates:

```text
dist-arm-wrapper-dynarmictest8
```

Launch:

```text
dist-arm-wrapper-dynarmictest8\RUN_DYNARMIC_INTERACTIVE.cmd
```

The full source package includes `game.apk`, the complete source/vendor tree, build scripts, patches and licenses.

## Controls

- Left mouse button: touch begin/end
- Mouse drag: touch move
- Space or Up Arrow: gameplay press/release
- Escape: Android Back
- Text and Backspace: editor text input
- Window deactivate/reactivate: Android pause/resume

While the Android IME/text field is open, Space is sent only as text and no gameplay touch is generated.

## Test8 focus

- memory-backed read-only `game.apk` handles using the already-loaded APK image
- direct guest-buffer `fread`/`fwrite` without per-call temporary vectors
- hot paths for the libc calls dominating ZIP and level parsing
- sampled import-history logging instead of allocating a diagnostic string for every trap
- clean host shutdown when the guest calls `terminateProcess`
- gameplay Space suppression while editor/name/description text input is active
- retained Test7 mapped stdio, save stability, audio bridge, allocator and fatal diagnostics

Test8 intentionally does **not** cache parsed level/editor objects. Those objects are mutable and caching them across edits could return stale or corrupt data. It accelerates the APK/resource source path instead, which also benefits first-open menus and textures.

See `DYNARMICTEST8-NOTES.md` for technical details.
