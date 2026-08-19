# Geometry Dash Wrapper 0.9.6-gdpsfixes3

`gdpsfixes3` is an Android-only maintenance branch focused on GDPS reliability
and legacy Geometry Dash compatibility. The abandoned experimental iOS backend
remains removed.

## New in GDPSFixes3: complete legacy color-picker resource repair

The GDPSFixes2 test proved the plist fallback worked but the color picker still
crashed in both 1.0 and 1.01. Static auditing of the exact 1.0 ARM binary shows
the picker immediately loads a second resource:
`extensions/CCControlColourPickerSpriteSheet.png`. A failed texture load then
reaches `CCSpriteBatchNode::updateBlendFunc` with a null texture.

GDPSFixes3 now:

- mirrors both standard/HD picker plist and PNG resources into the emulated
  Android `extensions` directory;
- repairs failed ZIP/minizip `extensions/...` lookups to the real APK asset;
- supports the older `CCFileUtils` ZIP-function Thumb prologues used by GD 1.0,
  which previously caused both host ZIP hooks to be skipped (`count=0`);
- keeps the newer layouts used by builds such as 1.01 supported as before.

The 1.0 log should now report:

`RESULT: DYNARMIC_LEGACY_GD100_CCFILEUTILS_COMPAT zip-hooks=2 expected=2`

Save/write paths are not redirected.

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
