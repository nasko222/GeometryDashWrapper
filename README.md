# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest13

DynarmicTest13 runs the ARMv5TE Android build of early Geometry Dash on 64-bit Windows through Dynarmic. Test12 established working GDPS upload/download and browser links. Test13 focuses on low-end-PC performance and useful diagnostics.

## Test13 changes

- Replaces repeated linear guest-memory mapping searches with a constant-time 4 KiB page lookup.
- Uses one bounds check and one copy for 16/32/64-bit guest memory accesses.
- Caches OpenGL host function pointers and argument metadata.
- Buffers routine logging instead of flushing the disk on every input/JNI/file event.
- Creates a per-frame “debug everything” profile with CPU, GPU, swap, imports, GL, draw, heap, and scene information.
- Writes a compact summary containing percentiles, threshold counts, hot imports, sampled host costs, and the 50 worst frames.

## Build

Run:

```text
BUILD_DYNARMIC_X64.cmd
```

Output:

```text
dist-arm-wrapper-dynarmictest13
```

Run `RUN_DYNARMIC_INTERACTIVE.cmd`. A custom GDPS APK may be passed to `build-dynarmic-x64.ps1` or copied as `game.apk` into the output folder.

## Send these files after testing

```text
gd-dynarmic-interactive.log
gd-dynarmic-profile.csv
gd-dynarmic-profile-summary.txt
```

The normal launcher enables the profiler automatically. For an A/B run without profiling overhead:

```text
GeometryDashDynarmicProbe.exe game.apk --no-profile --log=gd-dynarmic-no-profile.log
```

## Important startup markers

```text
RESULT: DYNARMIC_GUEST_PAGE_LOOKUP_READY
RESULT: DYNARMIC_DEBUG_EVERYTHING_READY
RESULT: DYNARMIC_OPENGL_IMPORT_CACHE_READY
RESULT: DYNARMIC_GPU_TIMER_READY
```

`DYNARMIC_GPU_TIMER_UNAVAILABLE` is not fatal; CPU, import, swap, draw, and heap profiling still work.
