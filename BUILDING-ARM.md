# Building Geometry Dash ARM Wrapper 0.9.4-arm-v22beta-bringup8

## Windows x64

No APK is included. Pass the beta APK explicitly:

```bat
BUILD_V22BETA_X64.cmd "D:\Games\geometry-dash-2.2-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-bringup8\
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup8\RUN_V22_SELECTED_APK.cmd
```

Use the normal wrench-and-hammer button from an entry in My Levels. Bringup8 patches that exact `EditLevelLayer::onEdit` callback. F2 is no longer intercepted.

The build script prepares pinned portable versions of Zig 0.14.1, CMake 3.31.10, Ninja 1.13.2, Boost 1.84.0, and Dynarmic commit `a41c380246d3d9f9874f0f792d234dc0cc17c180`.

The external APK is copied only into the generated local distribution directory. It is never part of the source archive.

## Raw-library probe

Building without an APK uses the included raw `libcocos2dcpp.so` and creates a probe-only launcher:

```bat
BUILD_V22BETA_X64.cmd
dist-arm-wrapper-v22beta-bringup8\RUN_V22_RAW_SO_PROBE.cmd
```

Raw mode validates mapping, relocation, constructors, `JNI_OnLoad`, ARMv7 instructions, exclusive memory and import coverage. Assets and interactive gameplay require a complete APK.
