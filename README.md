# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 bringup4

This is a separate ARMv7-A / Thumb-2 / VFPv3 / NEON bring-up branch. It does not replace the stable ARMv5 Test14-fix1 branch.

## Build with an external APK

No APK is included in this source package.

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-2.2-beta.apk"
```

Run the generated launcher:

```bat
dist-arm-wrapper-v22beta-bringup4\RUN_V22_SELECTED_APK.cmd
```

## Bringup4 targets

- audible menu and level music through the host FMOD 1.05.04 compatibility bridge;
- a valid 60 Hz Android refresh-rate response for editor/game simulation;
- recovery from the observed missing level setup-header element instead of the `0x30` null crash;
- detailed first-call FMOD diagnostics for the next runtime log.

Expected markers include:

```text
RESULT: DYNARMIC_X64_ARMV7_FEATURE_SMOKE_OK thumb2=1 vfpv3=1 neon=1 exclusive=1 guest=v7A host=x86_64
RESULT: DYNARMIC_V22_REFRESH_RATE_BRIDGE hz=60
RESULT: DYNARMIC_V22_FMOD_BRIDGE_READY ... version=0x00010504 deferred-music=1
```

When a beta level lacks element zero in its split setup header, the log reports:

```text
WARNING: V22 level setup header missing; substituted default empty settings string
```

That guard is deliberately narrow and applies only to the symbol-relative instruction reached in the supplied newer beta.
