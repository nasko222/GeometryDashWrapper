# Geometry Dash Wrapper 0.9.6-gdpstweaks10

Cross-version Windows wrapper for Geometry Dash Android builds.

## gdpstweaks10

This branch removes the desktop pause-button and cursor-hiding experiments completely and continues the ARMv7 stock-2.2 editor restoration from the latest runtime logs.

### Pause button and cursor rollback

The wrapper no longer exposes or implements `REMOVE_PAUSE_BUTTON` or `HIDE_CURSOR_WHEN_PLAYING` on x86, legacy ARM, or ARMv7. The game owns its native pause button and Windows cursor behavior again.

Removing the x86 feature also removes its recurring pause/cursor gameplay polling path, which is the main wrapper-side suspect for the periodic hitch reported while touching/swiping the 1.5/1.6 level selector.

### Stock 2.2 beta editor restoration

Recognized stock ARMv7 editor profiles remain:

- 9,144,004-byte primary library — early 2019 Lite beta
- 9,541,500-byte primary library — 2022 Lite beta
- 9,578,364-byte primary library — 2023/SubZero beta

The latest logs showed all three restored stock editors reaching `EditorUI::create()` and then crashing when reduced APKs returned a null `CCSpriteFrame`. Tweaks10 replaces one-off sprite aliases with a wrapper-side `CCSpriteFrameCache::spriteFrameByName` bridge. Native lookup always runs first; only a missing frame while a recognized stock editor is active receives a known-valid stock placeholder, and the requested missing name is logged.

The host editor initializer also carries forward the known stock/mod initializer corrections from tweaks8/9: strict level-setup decoding, correct late-vector layout, base-game-layer initialization, grid z-order, hidden editor marker, high-capacity option, background state, and ground visibility.

### 2.2 editor playtest isolation

Wrapper-owned editor overlay/visibility mutation is suspended while the stock editor is in playtest. The persistent playtest fields were verified from each exact binary's `LevelEditorLayer::onPlaytest()` and `onStopPlaytest()`:

- 2019: `LevelEditorLayer + 0x4F0`
- 2022: `LevelEditorLayer + 0x2C58`
- 2023: `LevelEditorLayer + 0x2C8C`

This targets the freeze observed when pressing Play in the 2023 editor while the wrapper was still running its camera/visibility repair every frame.

No modded APK or `libgame.so` is bundled with this source package.

See `GDPSTWEAKS10.md` and `GDPSTWEAKS10-VERIFICATION.txt` for implementation and verification notes. Historical `GDPSTWEAKS*.md` files are retained as branch history.
