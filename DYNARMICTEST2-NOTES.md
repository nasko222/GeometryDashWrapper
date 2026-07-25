# DynarmicTest2 fix 1

This is the first Dynarmic build that executes authentic Geometry Dash code beyond a smoke test.

## Build

```cmd
BUILD_DYNARMIC_X64.cmd
```

The existing Dynarmic/Boost/Zig/CMake/Ninja caches are reused. The expected output directory is:

```text
dist-arm-wrapper-dynarmictest2-fix1
```

Run `RUN_DYNARMIC_PROBE.cmd`.

## Expected success markers

```text
RESULT: DYNARMIC_RELOCATION_OK
RESULT: DYNARMIC_CONSTRUCTORS_OK count=238
RESULT: DYNARMIC_JNI_ONLOAD_OK result=0x00010004
RESULT: DYNARMIC_BRINGUP2_OK
```

## Diagnostic behavior

The constructor bridge implements the imports known to be required by the working Unicorn path. Unknown imports are logged as:

```text
Dynarmic permissive constructor stub: <symbol> -> 0
```

Import, JNI and return SVC callbacks explicitly halt the Dynarmic JIT before host-side dispatch. The halt is cleared before processing the callback so nested `pthread_once` initializers can execute safely.

A crash/failure now prints the exact constructor index, guest PC, LR, SP, CPSR, halt reason, memory fault, exception or import name to both the console and `gd-dynarmic-probe.log`.

`--relocate-only` is available to verify only APK extraction, mapping and relocation.

## Scope

This milestone does not call `nativeInit` or open a window. DynarmicTest3 will connect `nativeSetPaths`, JNI, OpenGL, file and audio bridges and target the first real frame.
