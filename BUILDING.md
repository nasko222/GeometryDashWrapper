# Building 0.9.5-unified1

## Requirements

Use 64-bit Windows 10/11 with Python 3 and Git for Windows. The command files
download verified portable Zig, CMake, Ninja, and Boost files into
`.build-tools/`; nothing is installed system-wide. The first ARM build also
checks out the pinned public Dynarmic revision from GitLab.

## x86

Run:

```bat
BUILD_X86.cmd
```

Optional APK packaging:

```bat
BUILD_X86.cmd "D:\APKs\game.apk"
```

Output: `dist-unified/x86/GeometryDashWrapper.exe`.

## Both ARM backends

Run `BUILD_DYNARMIC.cmd`. One Dynarmic build emits:

- `dist-unified/arm-legacy/GeometryDashArmLegacy.exe`
- `dist-unified/armv7/GeometryDashArmV7.exe`

The normal launchers avoid heavy profiling. Use each backend's separate
`RUN_DEBUG.cmd` only when collecting a regression log.

## Everything

Run `BUILD_ALL.cmd`. It preserves all three sibling output folders and copies
the automatic APK selector into `dist-unified/`.

The source archive contains no APK. ARM builds never require or package one;
copy the APK you are testing into the appropriate output folder as `game.apk`.
