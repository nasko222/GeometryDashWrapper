# Building the ARMv7 wrapper

## Bringup19 selected-APK build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\game-v22beta-selected.apk"
```

The portable builder downloads its pinned Zig, CMake, Ninja, Boost and Dynarmic
dependencies into `.build-tools`, then creates:

```text
dist-arm-wrapper-v22beta-bringup19-selected-desktop-network\
```

This branch targets the 144,490,721-byte selected APK. It does not package a
`libgame.so` donor and does not include save-recovery scripts.
