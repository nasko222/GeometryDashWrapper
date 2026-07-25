# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest3-fix1

## Why Fix1 exists

The first DynarmicTest3 run reached all of these milestones successfully:

- x64 Dynarmic ARMv5TE execution
- relocation of the authentic `libgame.so`
- all 238 constructors
- authentic `JNI_OnLoad` returning `0x00010004`
- `nativeSetPaths`
- Win32 OpenGL host creation

It then stopped at `nativeInit` because Test3 imposed an artificial 500,000,000 guest-tick limit. No invalid memory access, missing import, JNI failure, interpreter fallback, exception, or OpenGL failure was reported.

## Runtime changes

`nativeInit` now has no fixed guest-tick ceiling. It is protected by a 120-second wall-clock guard, which prevents a real infinite loop while allowing large one-time asset initialization workloads to finish.

Each `nativeRender` call similarly uses an unlimited guest-tick allowance with a 30-second wall-clock guard.

While a long guest call is running, the log receives a progress line every five seconds containing:

- elapsed wall time
- estimated guest ticks
- PC, LR, SP, and CPSR
- nearest dynamic symbol and ELF offset
- recent import/JNI/JavaVM trap history
- busiest imported functions

A timeout prints the same information directly in the terminal.

## Logs

`RUN_DYNARMIC_FIRST_FRAME.cmd` writes:

```text
gd-dynarmic-first-frame.log
```

`RUN_DYNARMIC_PROBE_ONLY.cmd` writes:

```text
gd-dynarmic-probe-only.log
```

The probe-only test can no longer overwrite the failed first-frame log.

## Run

```cmd
BUILD_DYNARMIC_X64.cmd
dist-arm-wrapper-dynarmictest3-fix1\RUN_DYNARMIC_FIRST_FRAME.cmd
```
