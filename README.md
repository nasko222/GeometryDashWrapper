# Geometry Dash Wrapper 0.9.6-gdpsfixes2

`gdpsfixes2` is an Android-only maintenance branch focused on GDPS reliability
and legacy Geometry Dash compatibility. The abandoned experimental iOS backend
remains removed.

## New in GDPSFixes2: legacy color picker

Geometry Dash 1.0 and 1.01 can request the cocos2d color-picker sheet through
`/data/data/<package>/extensions/CCControlColourPickerSpriteSheet.plist` even
though the packaged resource lives under APK `assets/`, commonly as the `-hd`
variant. The old wrapper translated that request only into the writable save
directory, returned fopen failure, and the game then crashed inside
`CCSpriteBatchNode::updateBlendFunc`.

The ARM file bridges now fall back from a missing read-only `extensions/...`
request to the corresponding APK asset, trying both the exact basename and the
`-hd` variant. The returned data uses the existing memory-backed FILE path, so
normal cocos2d parsing can continue. Saves and write paths are untouched.

## Carried fixes

- Large ARM/GDPS level uploads are no longer chopped at 4095 formatted bytes.
- MCI music seeks stop the alias before seeking for Wine/Proton compatibility.
- Legacy ARM nonblocking sockets no longer synchronously stall on bad network.

## Included backends

- x86 native Android wrapper for supported x86 builds.
- Dynarmic legacy ARM backend for Geometry Dash 1.0-1.4-era ARM APKs.
- Dynarmic ARMv7 backend for later ARMv7 builds / existing 2.2-beta path.

## Still open

- Editor WASD/Q shortcuts require a safe verified transform callback ABI.
- Cursor hiding and pause-button removal are deliberately not included.

## Building

Run `BUILD_ALL.cmd` on Windows. No APK or proprietary game executable is bundled.
