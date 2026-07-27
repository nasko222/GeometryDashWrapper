# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup7

Separate ARMv7-A / Thumb-2 / VFPv3 / NEON bring-up branch. It does not replace the stable ARMv5 Test14-fix1 branch.

## Build with an external APK

No APK is included in this source package.

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-2.2-beta.apk"
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup7\RUN_V22_SELECTED_APK.cmd
```

## Important editor workaround

The beta's visible editor button is known to do nothing on some real Android devices as well as this wrapper. In Bringup7, press **F2** from the menu or Creator screen. F2 calls the beta's genuine `CreatorLayer::onMyLevels` function directly and does not depend on the broken button callback.

## Bringup7 changes

- Restores the beta's original C++ `ZipUtils::decompressString` implementation completely.
- Hooks only the eight-byte C-style `ZipUtils::ccInflateMemory(unsigned char*, unsigned int, unsigned char**)` boundary.
- The beta itself now performs Base64 decoding, hidden return-object handling, `std::string` construction, copying and destruction exactly as compiled.
- Adds F2 direct My Levels/editor entry for both supplied native beta builds.
- Retains the `canPlayOnlineLevels()` force-true patch found in the optional companion `libgame.so`.
- Detects optional `lib/armeabi-v7a/libgame.so` but does not execute its Android Dobby loader.
- Retains FMOD 1.05.04-compatible music, 60 Hz timing and ARM exclusive memory.

Expected startup markers:

```text
RESULT: DYNARMIC_V22_LOW_LEVEL_INFLATE_HOOK_READY count=1 ... original-cpp-decompress=1
RESULT: DYNARMIC_V22_DIRECT_EDITOR_HOTKEY_READY key=F2 target=0x...
```

Expected level marker:

```text
[host] V22 ccInflateMemory input=123801 output=1241094 ... original-cpp-string-path=1
```

Expected F2 marker:

```text
[host] V22 F2 direct editor entry requested
RESULT: DYNARMIC_V22_DIRECT_EDITOR_ENTERED source=F2
```
