# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest3

## Purpose

DynarmicTest3 is the first x64 Dynarmic build that crosses from ELF/JNI bring-up into the real Cocos2d-x runtime. It executes the authentic `nativeSetPaths`, creates a Win32 OpenGL host window, executes `nativeInit(1280, 720)`, and enters `nativeRender`.

This is an iterative first-frame milestone, not yet a replacement for the stable Unicorn wrapper. Any unsupported import, JNI table slot, invalid guest memory access, or unavailable OpenGL entry point is written to both the console and `gd-dynarmic-probe.log` with the current function label.

## Implemented in Test3

- Existing x64 ARMv5TE Dynarmic execution, relocations, 238 constructors, and JNI 1.4 startup.
- Authentic `nativeSetPaths` and absolute APK path delivery through a guest JNI string.
- Guest JNI class, method, object, string, primitive-array, preference, save-file, activity, and audio-stub services.
- Guest `FILE*` proxy layer for the APK and writable Android data paths.
- 32-bit guest zlib stream translation to the bundled host zlib.
- Constructor/runtime memory, string, formatting, scanning, time, locale, pthread, semaphore, math, and file imports.
- Win32 x64 OpenGL context and typed marshalling for pointers, floating-point parameters, shaders, textures, buffers, uniforms, and draw calls.
- A 180-frame first-frame launcher and a probe-only launcher.

## Build

```cmd
BUILD_DYNARMIC_X64.cmd
```

The existing Dynarmic, Boost, Zig, CMake, and Ninja caches are reused. The output is:

```text
dist-arm-wrapper-dynarmictest3
```

## Run

```text
RUN_DYNARMIC_FIRST_FRAME.cmd
```

For constructor/JNI regression testing without creating a window:

```text
RUN_DYNARMIC_PROBE_ONLY.cmd
```

## Expected milestone lines

```text
RESULT: DYNARMIC_CONSTRUCTORS_OK count=238
RESULT: DYNARMIC_JNI_ONLOAD_OK result=0x00010004
RESULT: DYNARMIC_PATHS_SET
RESULT: DYNARMIC_OPENGL_HOST_OK
RESULT: DYNARMIC_NATIVE_INIT_RETURNED
RESULT: DYNARMIC_RENDER_LOOP_ENTERED
RESULT: DYNARMIC_FIRST_FRAME_OK
RESULT: DYNARMIC_BRINGUP3_OK
```

A failure is still useful. Send the full console output and `gd-dynarmic-probe.log`; the log identifies the exact guest call, import, JNI slot, OpenGL function, PC, or memory address that stopped the milestone.

## Current intentional limitations

- Android audio calls are non-blocking state/logging stubs in Test3.
- Network and background-thread imports do not perform real online work.
- Input and lifecycle event forwarding are reserved for Test4.
- This build has not been runtime-tested on Windows by the package author; the user-side run is the validation step.
