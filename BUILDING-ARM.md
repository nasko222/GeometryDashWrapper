# Building Geometry Dash ARM Wrapper 0.9.4-arm-v22beta-bringup13

Use a fresh extracted source directory on 64-bit Windows:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

The build script pins:

- Dynarmic commit `a41c380246d3d9f9874f0f792d234dc0cc17c180`
- Zig `0.14.1`
- Boost headers `1.84.0`

The output directory is:

```text
dist-arm-wrapper-v22beta-bringup13\
```

Launch the copied APK with:

```bat
dist-arm-wrapper-v22beta-bringup13\RUN_V22_SELECTED_APK.cmd
```

The newer beta's `lib/armeabi-v7a/libgame.so` is required for the repaired editor. Earlier APKs without that module still receive the level-data and level-settings fixes.

No APK is included in the source package.
