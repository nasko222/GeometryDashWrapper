# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 bringup3

Separate experimental ARMv7-A/Thumb-2/VFPv3/NEON branch. The stable ARMv5 line is untouched.

## Build without bundling APKs

The source archive intentionally contains **no APK files**.

Probe the included raw library:

```bat
BUILD_V22BETA_X64.cmd
```

Build with one of your own beta APKs:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

Then use `RUN_V22_SELECTED_APK.cmd` in the generated dist folder. You may also manually place APKs beside the executable as `game-v22beta.apk` or `game-v22beta1.apk` and use the separate newer/earlier launchers.

## Bringup3 changes

- Implements Dynarmic `MemoryWriteExclusive8/16/32/64` compare-and-write callbacks.
- Adds an LDREX/STREX startup smoke test matching constructor 1's atomic loop.
- Accepts both verified `CCApplication::openURL` Thumb prologues used by the supplied betas (`0xB530` and `0xB51F`).
- Keeps startup music pre-cache, FMOD compatibility, profiling and dual-beta diagnostics.
- Does not include any APK in the source package.

Future unified x86/ARMv5/ARMv7 auto-selection remains intentionally deferred.
