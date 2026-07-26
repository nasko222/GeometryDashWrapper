# Geometry Dash ARM Wrapper — 2.2 Beta ARMv7 Bring-up 1

This directory is an experimental branch for an ARMv7 Geometry Dash 2.2 beta native library. The stable 1.0–1.4/Test14 source history is retained inside this package, but this branch builds to its own `dist-arm-wrapper-v22beta-bringup1` directory.

## Build the supplied raw library probe

```bat
BUILD_V22BETA_X64.cmd
```

After building, run `RUN_V22_RAW_SO_PROBE.cmd`. It attempts the 349 constructors and `JNI_OnLoad`, then writes a detailed log and import manifest.

## Build with the complete APK

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\2.2-beta.apk"
```

After building, run `RUN_V22_APK_INTERACTIVE.cmd`.

The complete APK is required to reach real `nativeInit`, because the uploaded file contained only `libcocos2dcpp.so`; Java classes, manifest data, assets and any additional APK libraries are not available in raw-library mode.

See `V22BETA-BRINGUP1-NOTES.md` and `V22BETA-STATIC-AUDIT.txt` for the implemented compatibility work and current limitations.
