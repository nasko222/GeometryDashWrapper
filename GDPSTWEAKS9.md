# Geometry Dash Wrapper 0.9.6-gdpstweaks9

This branch targets three regressions reported after gdpstweaks8.

## Stock 2.2 beta editor assets

The 2019, 2022 and 2023 stock reduced APKs now reach `EditorUI::create()` but request some sprite-frame names that are not present in their frame-cache plists. The modded 2023 editor APK contains extra full-version/editor art, but this source package does not copy or redistribute any donor assets.

Before `EditorUI::create()`, ARMv7 now ensures `GJ_GameSheet03.plist` is loaded, obtains the stock `GJ_arrow_02_001.png` frame, and installs aliases only when these known missing names are absent:

- `GJ_button_04.png`
- `pixelb_03_01_001.png`

The aliases are intentionally placeholders. Their purpose is to keep missing optional art from dereferencing a null `CCSpriteFrame` while preserving the stock APK and allowing the next missing dependency, if any, to be observed cleanly.

## ARMv7 pause removal

The earlier render-time hide was too late or did not reliably observe the beta `UILayer`. The supported 2019/2022/2023 profiles now patch the `UILayer` vtable entry for `init()` to a small guest thunk. The thunk calls the untouched original initializer, reads the exact pause member (`+0x1B0`, `+0x1BC`, or `+0x1C0`), calls `CCNode::setVisible(false)`, and returns the original init result. This occurs before `UILayer::create()` returns and before a gameplay frame can present the button. Escape handling is unchanged.

## x86 1.5/1.6 level-selector hitch

Pause/cursor restoration had put a recursive Cocos scene-tree walk back into the recurrent render path. On old x86 builds that scan occurred periodically while swiping level pages and caused visible stalls. The recurrent detector now uses only the small `GameManager` state block. Full scene traversal is reserved for paths that actually need modal/editor discovery.

## x86 first-frame pause removal

For 32-bit x86 builds the wrapper derives the first pause-item `CCMenuItemSpriteExtra::create` call inside `UILayer::init()` and redirects only that call to a wrapper shim. The shim calls the original factory and immediately applies `setVisible(false)` to the returned item. The prior runtime hide remains as a fallback if the creation call cannot be derived.

## Runtime status

Windows/APK runtime execution is not available in this build environment. Source/static verification only; the reported behaviors require Windows retesting.
