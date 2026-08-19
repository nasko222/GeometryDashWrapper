# 0.9.6-gdpsfixes3

Branch: `gdpsfixes3`

## Why GDPSFixes2 did not work

The new GDPSFixes2 runtime logs prove the plist fallback itself succeeded in
both Geometry Dash 1.0 and 1.01, but both builds still faulted at guest address
`0x54` inside `cocos2d::CCSpriteBatchNode::updateBlendFunc`.

The deeper 1.0 ARM audit shows the complete picker path:

1. `CCControlColourPicker::init` loads
   `extensions/CCControlColourPickerSpriteSheet.plist`.
2. It immediately creates a `CCSpriteBatchNode` for
   `extensions/CCControlColourPickerSpriteSheet.png`.
3. `CCSpriteBatchNode::initWithFile` asks `CCTextureCache::addImage` for that
   PNG.
4. The relative path goes through `CCFileUtils::getFileDataFromZip`.
5. If the texture load returns null, `initWithTexture` eventually reaches
   `updateBlendFunc` and dereferences the null texture object.

GDPSFixes2 only rescued the absolute plist `fopen`; it did not guarantee that
the PNG's ZIP path reached the host fallback.

There was an additional 1.0-specific blocker: its two `CCFileUtils` ZIP methods
start with older Thumb prologues (`B530` and `B500`). The wrapper hook installer
only accepted the later layouts (`B5F0` and `B570`), so the GD 1.0 log reported
`DYNARMIC_CCFILEUTILS_ZIP_HOOKS_READY count=0`. GD 1.01 already reported
`count=2`.

## GDPSFixes3 fix

GDPSFixes3 repairs the complete old-cocos resource route:

- mirrors the standard and HD color-picker plist/PNG pairs from the APK into the
  emulated writable `extensions` directory at startup;
- retries failed `extensions/...` ZIP names against the actual APK asset
  basename and then the old `-hd` form;
- applies that compatibility rule to `getFileDataFromZip`,
  `existFileDataFromZip`, and accelerated minizip lookup;
- **accepts the verified early-GD Thumb prologues for the two CCFileUtils ZIP
  hooks**, so Geometry Dash 1.0 now reaches the host resource fallback instead
  of silently staying on its unhooked guest ZIP path;
- leaves normal save/write paths untouched.

Expected 1.0 startup markers include:

`RESULT: DYNARMIC_LEGACY_GD100_CCFILEUTILS_COMPAT zip-hooks=2 expected=2`

`RESULT: DYNARMIC_ANDROID_EXTENSION_RESOURCE_MIRROR_READY ready=4 available=4 ...`

Opening the picker may then log an `APK ZIP extension path fallback` or
`minizip extension path fallback` for the PNG, depending on the exact path the
build takes.

## Carried fixes

- >4095-byte ARM/GDPS formatted upload truncation fix.
- Stop-before-seek MCI retry/StartPos compatibility behavior.
- Legacy ARM nonblocking connect/recv behavior for bad-network freezes.
- Android-only branch; experimental iOS backend remains removed.

## Still pending

- Editor WASD/Q transform shortcuts need a verified editor callback ABI.
- Cursor hiding and pause-button removal remain intentionally excluded.
