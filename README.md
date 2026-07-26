# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest9

DynarmicTest9 runs the ARMv5TE Android build of early Geometry Dash on 64-bit Windows through Dynarmic.

## Main changes in Test9

- Host-level cocos2d APK member cache instead of millions of guest minizip calls.
- Memory cache plus persistent validated cache in `save/apk-member-cache`.
- NVIDIA Optimus and AMD PowerXpress high-performance GPU preference exports.
- Actual OpenGL vendor/renderer logging.
- Experimental Winsock bridge for the game’s bundled libcurl.
- Cooperative execution of the game’s `CCHttpClient` worker.
- Reduced repetitive touch-move logging.
- Retains Test6 allocator, Test7 save/audio, and Test8 Exit/Space fixes.

## Build

On 64-bit Windows, run:

```cmd
BUILD_DYNARMIC_X64.cmd
```

The builder creates:

```text
dist-arm-wrapper-dynarmictest9
```

Launch with:

```text
dist-arm-wrapper-dynarmictest9\RUN_DYNARMIC_INTERACTIVE.cmd
```

The first launch may populate audio and APK-member caches. Later launches reuse them.

## Useful log markers

```text
RESULT: DYNARMIC_CCFILEUTILS_ZIP_HOOKS_READY count=2
RESULT: DYNARMIC_APK_MEMBER_INDEX_READY entries=...
RESULT: DYNARMIC_WINSOCK_BRIDGE_READY version=2.2
RESULT: DYNARMIC_OPENGL_DEVICE vendor=... renderer=...
Dynarmic guest call timing: nativeTouchesEnd elapsed_ms=...
```

For internet testing, open an online menu and keep the complete log. DNS and connection attempts appear as `[host] DNS ...` and `[host] Socket connect ...`.

## Source integrity

`game.apk`, Dynarmic build files, zlib, stb_vorbis, licenses, and all source dependencies are included. No cleanup step removes the APK or required source trees.
