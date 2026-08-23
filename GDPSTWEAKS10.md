# Geometry Dash Wrapper 0.9.6-gdpstweaks10

This build is a rollback of the desktop pause/cursor experiments plus a focused
ARMv7 stock-2.2 editor recovery pass based on the latest runtime logs.

## Pause button / cursor rollback

The wrapper no longer contains the `REMOVE_PAUSE_BUTTON` or
`HIDE_CURSOR_WHEN_PLAYING` settings. Their x86, legacy-ARM and ARMv7 runtime
implementations have been removed rather than merely disabled. The game owns
its native pause button and Windows cursor behavior again.

Removing the x86 feature also removes its render-time pause/cursor gameplay
polling path. This is intended to avoid the periodic old-x86 UI hitch reported
while swiping the 1.5/1.6 level selector.

## Stock 2.2 editor

The three exact stock ARMv7 layouts remain profile-gated:

- 9,144,004 bytes — early 2019 Lite beta
- 9,541,500 bytes — 2022 Lite beta
- 9,578,364 bytes — 2023/SubZero beta

The latest logs show all three restored editors now reach `EditorUI::create`
and fail when reduced spin-off APKs return a null `CCSpriteFrame`. Instead of
adding guessed frame aliases one at a time, the wrapper now bridges stock
`CCSpriteFrameCache::spriteFrameByName` calls. Native lookup always runs first. During recognized stock-editor runtime, a missing frame is replaced with a known-valid stock frame and its exact requested name is logged. Outside a recognized active stock editor, missing lookups remain native/null.

The host initializer also closes known differences with the working 2023 editor
initializer:

- grid z-order follows stock GameManager variable `0039`;
- the hidden `d_cross_01_001.png` editor/playtest marker is created and stored;
- high-capacity mode follows stock variable `0066`;
- the 2023 post-background state bytes match the donor sequence;
- final ground visibility follows stock variable `0037`.

No donor graphics, mod libraries or APK payloads are bundled.

## Editor playtest

Wrapper camera-cull/timeline editor mutations are suspended while an editor
playtest is active. The stock game is allowed to own that transition instead of
having the host visibility pass mutate editor objects during Play. The persistent
state fields are verified from both `LevelEditorLayer::onPlaytest()` (set to 1)
and `onStopPlaytest()` (reset to 0):

- 2019: `+0x4F0`
- 2022: `+0x2C58`
- 2023: `+0x2C8C`

## Runtime status

Source/static verification is performed in this environment. Windows gameplay
runtime confirmation is still required for stock editor entry, editor Play and
the reported 1.5/1.6 selector hitch.
