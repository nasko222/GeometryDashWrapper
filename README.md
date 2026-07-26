# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 bringup2

Separate experimental ARMv7-A/Thumb-2/VFPv3/NEON branch. The stable ARMv5 line is not replaced.

## Build

```bat
BUILD_V22BETA_X64.cmd
```

By default the build packages both included beta APKs and the raw `libcocos2dcpp.so`. It creates separate launchers and output logs for the earlier and newer betas.

## Bringup2 changes

- Adds the required Dynarmic global exclusive monitor.
- Supports the earlier beta's FMOD stream-buffer getters/setters.
- Pre-caches all APK music before native initialization.
- Keeps raw-library probe mode.
- Removes inherited changelog/note clutter from unrelated versions.

Future direction: one unified x86/ARMv5/ARMv7 launcher should auto-select x86 first, with a later configuration override to prefer ARM. That merge is intentionally not implemented here.
