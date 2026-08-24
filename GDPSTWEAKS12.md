# GDPSTWEAKS12

`0.9.6-gdpstweaks12` is a narrow regression fix based on the 2026-08-24 runtime logs from the x86 2017/SubZero family and the restored ARMv7 2019/2022/2023 2.2-beta editors.

## 1. ARMv7 restored-editor crash in EditorUI::create

Tweaks11 treated one GameManager pointer as the only current game/editor layer pointer. The exact binaries show two adjacent pointers with different consumers:

- 2019 `PlayLayer::init` stores the current `GJBaseGameLayer` at `GameManager + 0x158`, while `GameObject::createRotateAction` loads the editor pointer from `GameManager + 0x15C`.
- 2022/2023 `PlayLayer::init` stores the current `GJBaseGameLayer` at `GameManager + 0x168`, while `GameObject::createRotateAction` loads the editor pointer from `GameManager + 0x16C`.

The uploaded failures match those missing editor slots exactly: the 2019 run faults through a null base plus `0x40A`; the 2022 run faults through a null base plus `0x2B9A`; and the 2023 runs fault through a null base plus `0x2BCE`.

The host-restored editor now writes itself to both GameManager slots. The first remains the generic active `GJBaseGameLayer` slot used by input/player paths; the adjacent slot is the explicit editor pointer used by editor/GameObject helpers. The input resolver continues using only the generic active slot, so normal gameplay does not get redirected to the editor-specific field.

## 2. x86 editor hotkeys leaking into menus

Tweaks11 cached `EditorUI` against a guessed running `CCScene`. Cocos can retain an old scene while level-select or another menu is active, so A/D/W/S/Q/E could still hit the stale editor object and cause visible spikes.

Tweaks12 changes the shortcut gate:

- a shortcut is allowed only if `GameManager::sharedState()` currently contains a `LevelEditorLayer`;
- once discovered, the exact GameManager slot is cached, making normal editor hotkeys an O(1) pointer validation;
- if no editor is registered, the key is ignored immediately and the full Cocos scene tree is not walked;
- entering an editor performs only a bounded GameManager scan and then searches the confirmed editor subtree for `EditorUI` once;
- `CCDirector::getRunningScene()` is used when exported, avoiding the old heuristic scan of CCDirector memory and its retained-scene ambiguity.

This keeps the working 2017 x86 editor shortcuts while preventing editor actions from firing in level select and other menus.

## 3. Audio

No audio behavior is changed in this branch. The shared software-volume implementation from tweaks11 is retained byte-for-byte; the confirmed 1.0 music fix is intentionally left alone.

## Runtime-test boundary

The uploaded logs prove the tweaks11 ARM crash cause at the exact failing instructions. This environment cannot execute the Windows wrapper, so tweaks12 still needs a Windows test for the next runtime point after `EditorUI::create()` and for x86 menu hotkey suppression.
