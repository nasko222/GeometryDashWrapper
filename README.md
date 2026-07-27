# Geometry Dash Wrapper 0.9.5-unified1-fix1

This branch combines the three exact last known-good lines without Unicorn:

- **x86 native, highest priority:** `0.9.3-alpha3`
- **legacy ARM/Thumb:** `0.9.4-arm-dynarmictest14-fix1`
- **ARMv7 / Geometry Dash 2.2:** `0.9.4-milestone1`

The package contains no APK, extracted proprietary game library, executable, or
Unicorn dependency.

## Automatic backend selection

Put one APK at `dist-unified/game.apk` and run `dist-unified/RUN_AUTO.cmd`, or
pass an APK path to `RUN_AUTO.cmd`.

Selection order is explicit:

1. x86: `lib/x86/libcocos2dcpp.so` or `lib/x86/libgame.so`
2. legacy ARM: `lib/armeabi/libgame.so`
3. ARMv7/2.2: `lib/armeabi-v7a/libcocos2dcpp.so`

Therefore a multi-architecture APK always uses native x86 when available. The
legacy selector now correctly recognizes the `lib/armeabi/libgame.so` layout
used by the uploaded old-ARM APK.

## One save folder

All automatic and generated backend launchers run from `dist-unified/` and use:

```text
dist-unified/save/
```

There are no separate `x86/save`, `arm-legacy/save`, or `armv7/save-v22beta`
folders. Because different Geometry Dash versions can reuse the same save-file
names, back up `save/` before switching between substantially different APKs.

## Source layout

- `src/backends/x86/` — the alpha3 native x86 loader/JNI/runtime.
- `src/backends/arm_legacy/` — the DynarmicTest14-fix1 ARMv5TE/Thumb backend.
- `src/backends/armv7/` — the Milestone1 ARMv7/Thumb-2 backend.
- `src/shared/` — shared storage, audio, APK audio extraction, build metadata,
  and Android/Winsock translation.
- `cmake/` — one build graph for both ARM generations and one Dynarmic checkout.

## Build

- `BUILD_X86.cmd` builds x86.
- `BUILD_DYNARMIC.cmd` builds both Dynarmic backends.
- `BUILD_ALL.cmd` builds all three and creates the shared root save folder.

Normal ARM launchers avoid full tracing. Use each backend's `RUN_DEBUG.cmd` only
for regression logs. The F2 editor shortcut remains completely removed.
