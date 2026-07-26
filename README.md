# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup6

Separate ARMv7-A / Thumb-2 / VFPv3 / NEON bring-up branch. It does not replace the stable ARMv5 Test14-fix1 branch.

## Build with an external APK

No APK is included in this source package.

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-2.2-beta.apk"
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup6\RUN_V22_SELECTED_APK.cmd
```

## Bringup6 changes

- Builds decoded level strings through the beta's own relocated libstdc++ `std::string` byte routine and empty-string singleton instead of fabricating the COW representation.
- Validates the guest string byte-for-byte before returning it to `PlayLayer`.
- Forces `CreatorLayer::canPlayOnlineLevels()` to return true, reproducing the exact editor-unlock hook found in the newer APK's companion `libgame.so`.
- Detects and reports optional `lib/armeabi-v7a/libgame.so`; its Android Dobby loader is not executed.
- Retains the older `onOnlyFullVersion -> onMyLevels` redirect as a secondary fallback.
- Retains FMOD 1.05.04-compatible music, 60 Hz timing, ARM exclusive memory, dual-beta hooks, and host level decompression.

Expected markers:

```text
RESULT: DYNARMIC_V22_COMPANION_LIBGAME_DETECTED ... role=editor-hooks
RESULT: DYNARMIC_V22_DECOMPRESS_HOOK_READY ... guest_string_builder=0x...
RESULT: DYNARMIC_V22_CAN_PLAY_ONLINE_LEVELS_FORCE_TRUE count=1
```

A successful level decode now includes `guest_string=<same decoded size>`:

```text
[host] V22 decompressString ... output=1241094 guest_string=1241094 status=ok
```
