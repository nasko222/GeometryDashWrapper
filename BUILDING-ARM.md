# Building Geometry Dash ARM Wrapper 0.9.4-arm-v22beta-bringup6

## Windows x64

No APK is included. Pass the beta APK explicitly:

```bat
BUILD_V22BETA_X64.cmd "D:\Games\geometry-dash-2.2-beta.apk"
```

The build script prepares pinned portable versions of Zig 0.14.1, CMake 3.31.10, Ninja 1.13.2, Boost 1.84.0, and Dynarmic commit `a41c380246d3d9f9874f0f792d234dc0cc17c180`.

Output:

```text
dist-arm-wrapper-v22beta-bringup6\
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup6\RUN_V22_SELECTED_APK.cmd
```

The external APK is copied only into that local output directory. It is never part of the source archive.

## Raw-library probe

Building without an APK uses the included raw `libcocos2dcpp.so` and creates a probe-only launcher:

```bat
BUILD_V22BETA_X64.cmd
dist-arm-wrapper-v22beta-bringup6\RUN_V22_RAW_SO_PROBE.cmd
```

Raw mode validates mapping, relocation, constructors, `JNI_OnLoad`, ARMv7 instructions, exclusive memory, and import coverage. Assets and interactive gameplay require a complete APK.

## Important Bringup6 markers

```text
RESULT: DYNARMIC_V22_DECOMPRESS_HOOK_READY ... guest_string_builder=0x...
RESULT: DYNARMIC_V22_CAN_PLAY_ONLINE_LEVELS_FORCE_TRUE count=1
RESULT: DYNARMIC_V22_COMPANION_LIBGAME_DETECTED ...
```

Preserve the complete `gd-v22beta-selected.log` after testing menu audio, official levels, My Levels, creating a level, editor playtest, pause/resume, and returning to the menu.
