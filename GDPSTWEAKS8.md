# Geometry Dash Wrapper 0.9.6-gdpstweaks8

Focused ARMv7 2.2-beta correction over gdpstweaks7.

## 2.2 stock editor restoration

- Corrects the 9,541,500-byte 2022 beta level setup field from `GJGameLevel+0x118` to the binary-verified `+0x11C`.
- Matches the working 2023 editor mod's late-vector construction: reserve/zero capacity 9999 while logical vector size remains zero. The 2022 late ABI uses the same policy.
- Replaces guest `ZipUtils::decompressString` restoration with strict host base64 + zlib/gzip inflate. `createObjectsFromSetup` is called only with validated GD setup text; failed decoding falls back to a known empty setup rather than passing corrupted bytes to the stock parser.
- Removes the old cross-field `GJGameLevel` string heuristic for the three known profiles. A verified setup field that is empty is treated as a new empty level instead of scanning unrelated name/description strings.
- The 2019 Lite beta explicitly loads its five existing `GJ_GameSheet*.plist` editor atlases before `EditorUI::create`, fixing the observed null `CCSpriteFrame` crash path.

## ARMv7 pause button

When `REMOVE_PAUSE_BUTTON=true` (default), the wrapper patches the first `CCMenuItemSpriteExtra::create` call inside `UILayer::init`. In the verified binaries this is the top-right pause item at UILayer+0x1B0 (2019), +0x1BC (2022), and +0x1C0 (2023). The wrapper-created Thumb thunk calls the original constructor, immediately calls `CCNode::setVisible(false)`, and returns the unchanged item before it can be added/rendered. Escape pause remains untouched.

The older per-frame hide remains as a fallback.

## Runtime status

Source/static verification is performed in the available Linux environment. Windows/APK runtime confirmation is still required.
