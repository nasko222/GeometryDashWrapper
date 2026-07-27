# Geometry Dash ARM Wrapper — 2.2 beta Bringup18

Bringup18 fixes the end-level crash found in the Bringup17 test log and finally
executes the selected beta's packaged feature hooks instead of merely detecting
them.

## Build selected beta

```bat
BUILD_V22BETA_X64.cmd "D:\APKs\selected-beta.apk"
```

Output: `dist-arm-wrapper-v22beta-bringup18-runtime-hooks\`

Use `RUN_V22_SELECTED_APK.cmd` for the safer feature set. Use
`RUN_V22_SELECTED_APK_ALL_HOOKS.cmd` to test every discovered feature group.

## Give stock SubZero the late-beta editor/features companion

Stock SubZero has a compatible late primary-library layout but does not contain
`libgame.so`. Pass the selected beta as a donor:

```bat
BUILD_V22BETA_X64.cmd "D:\APKs\SubZero.apk" "D:\APKs\selected-beta.apk"
```

The builder extracts only `lib/armeabi-v7a/libgame.so` into the generated dist
folder. Source packages contain neither APKs nor proprietary native libraries.

## Important boundary

The 95 MB early beta uses a different ABI and its `libgdkit.so` does not contain
the missing full editor. A late-beta donor is deliberately rejected for that
layout.

See `V22BETA-BRINGUP18-NOTES.md` for hook profiles and technical details.
