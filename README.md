# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup5

This is the separate ARMv7-A / Thumb-2 / VFPv3 / NEON branch. It does not replace the stable ARMv5 Test14-fix1 branch.

## Build with an external APK

No APK is included in this source package.

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-2.2-beta.apk"
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup5\RUN_V22_SELECTED_APK.cmd
```

## Bringup5 changes

- Replaces the beta's guest `ZipUtils::decompressString` path with a host URL-safe Base64 + GZIP/zlib decoder.
- Returns a valid ARM libstdc++ COW `std::string` containing the complete level setup, including every object.
- Removes Bringup4's empty-settings null recovery. Invalid level memory is fatal again instead of producing an empty level.
- Redirects `CreatorLayer::onOnlyFullVersion` to the real `CreatorLayer::onMyLevels` callback so the beta's editor gate opens My Levels.
- Retains Bringup4's working FMOD music bridge and 60 Hz refresh-rate bridge.

Expected startup markers:

```text
RESULT: DYNARMIC_V22_DECOMPRESS_HOOK_READY count=1 codec=base64url+gzip max_output_mb=64
RESULT: DYNARMIC_V22_CREATOR_EDITOR_UNLOCK_READY count=1 redirect=onOnlyFullVersion->onMyLevels
```

A real official-level load should log a large output, for example:

```text
[host] V22 decompressString input=165068 compressed=123801 output=1241094 encrypted=0 status=ok
```

When the gated creator button is invoked:

```text
[host] V22 creator full-version gate redirected to My Levels call=1
```
