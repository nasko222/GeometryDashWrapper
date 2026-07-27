# Geometry Dash Wrapper 0.9.5-unified1

This branch combines the three exact last known-good lines without Unicorn:

- **x86 native:** `0.9.3-alpha3`
- **legacy ARM/Thumb:** `0.9.4-arm-dynarmictest14-fix1`
- **ARMv7 / Geometry Dash 2.2:** `0.9.4-milestone1`

The package contains no APK, extracted proprietary game library, executable, or
Unicorn dependency.

## Source layout

- `src/backends/x86/` — the alpha3 native x86 loader/JNI/runtime.
- `src/backends/arm_legacy/` — the DynarmicTest14-fix1 ARMv5TE/Thumb backend.
- `src/backends/armv7/` — the Milestone1 ARMv7/Thumb-2 backend.
- `src/shared/` — one shared implementation of storage, audio, APK audio
  extraction, ELF definitions, build metadata, and Android/Winsock translation.
- `cmake/` — one build graph that compiles both ARM generations against one
  pinned Dynarmic checkout.

The version-specific emulation/JNI cores remain separate. They implement
different CPU generations and game ABIs, so forcing them into one giant runtime
would risk the working branches merely to reduce file count.

## Build and run

- `BUILD_X86.cmd` builds the 32-bit native x86 backend. Optionally pass an x86
  APK path as its first argument.
- `BUILD_DYNARMIC.cmd` builds both 64-bit Dynarmic backends.
- `BUILD_ALL.cmd` builds all three.

Output is written beneath `dist-unified/`. Put the relevant APK beside a backend
as `game.apk`, or use `RUN_AUTO.cmd D:\path\to\game.apk`; it selects x86,
legacy ARM, or ARMv7 by inspecting the APK library folders.

## Debug policy

Normal ARM launchers do not enable full tracing. Each ARM output also includes
`RUN_DEBUG.cmd`, which explicitly enables import dumps and frame profiling.
This keeps normal gameplay logs readable while preserving the diagnostics for a
real regression.

## F2 removal

The F2 editor shortcut is completely removed from the ARMv7 backend: no Windows
key handler, queued event, execution switch case, symbol lookup, readiness line,
or diagnostic path remains. Ordinary creator/editor buttons and their existing
compatibility bridges remain.
