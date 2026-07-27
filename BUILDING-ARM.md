# Building Geometry Dash ARM Wrapper 0.9.4-arm-v22beta-bringup17

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
dist-arm-wrapper-v22beta-bringup17\
```

Launch the copied APK with:

```bat
dist-arm-wrapper-v22beta-bringup17\RUN_V22_SELECTED_APK.cmd
```

The selected late beta can use a compatible
`lib/armeabi-v7a/libgame.so` editor initializer. Compatibility is checked from
the primary layout and initializer ABI rather than an APK filename or CRC.
Primary-only APKs receive the universal Creator/My Levels unlock, while a
base-only editor stub is never treated as a full editor implementation.

No APK is included in the source package.
