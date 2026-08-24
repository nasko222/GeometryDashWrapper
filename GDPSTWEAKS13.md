# GDPSTWEAKS13

`0.9.6-gdpstweaks13` is a rollback-and-repair branch for regressions observed in the 2026-08-24 `gdpstweaks12` runtime logs.

## 1. x86 editor controls restored without menu hotkey scans

Tweaks12 required a `LevelEditorLayer` pointer to be discoverable through `GameManager` before A/D/W/S/Q/E could be dispatched. The uploaded 1.50 x86 log proves that this layout does not register the editor there: every editor key was rejected with `GameManager has no active editor`.

Tweaks13 removes that hard gate. EditorUI discovery is tied to the actual Cocos running scene and caches both successful and unsuccessful results for that scene. Repeated hotkeys in level select or another non-editor scene therefore return immediately instead of recursively walking the Cocos tree on every key press. Hidden EditorUI remains a hard no-op, so edit shortcuts are not sent while editor Play owns input.

Older x86 Cocos builds often inline `CCDirector::getRunningScene()` and do not export it. Tweaks13 therefore derives `m_pRunningScene` from `CCDirector::drawScene()` when necessary. The available older x86 test binary resolves this field to `+0x6C`; a blind CCDirector scene scan is retained only as a last fallback.

## 2. ARMv7 2.2 editor Play crash: native CCArray storage

The restored editor could open in tweaks12, but Play crashed in the native Cocos array code. The logs show both forms of the same failure:

- `CCArray::removeAllObjects()` receives an object whose internal `ccArray*` is null;
- later `CCArray::addObject()` reaches `ccArrayDoubleCapacity()` and calls `realloc` with the same null native array structure.

The bug was in the wrapper restore helper: it treated any field that looked like a valid guest C++ object as already initialized. `GJBaseGameLayer::init()` can leave a valid-looking CCArray shell in LevelEditorLayer while the internal Cocos array pointer is still null.

Tweaks13 validates the inner `ccArray` before reusing an editor collection. The Cocos layouts are profile-specific and binary-verified:

- 2019 stock beta: `CCArray::_data` is at object `+0x20`;
- 2022 stock beta: `CCArray::_data` is at object `+0x30`;
- 2023 stock beta: `CCArray::_data` is at object `+0x30`.

The native `ccArray` header is also checked for a sane count/capacity relationship and backing element pointer. Invalid shells are replaced with a fresh retained `CCArray` created through the game's own Cocos function.

The exact Play arrays are also audited:

- 2019 `LevelEditorLayer::onPlaytest()` uses editor `+0x2A4`;
- 2022 uses editor `+0x350`;
- 2023 uses editor `+0x354`.

The 2019 `+0x2A4` array was missing from the host restoration list and is added in this branch.

## 3. Preview Mode transitions

The `0036` observer in tweaks12 did fire correctly, but transition handling was incomplete. When Preview Mode was turned off, the wrapper could immediately re-enable editor-only background suppression before the native game got a chance to restore its non-preview background. The wrapper also did not explicitly refresh preview animation/particle state after a toggle.

Tweaks13:

- gives both Preview ON and OFF transitions an eight-editor-frame native background-update grace window;
- schedules `LevelEditorLayer::updatePreviewAnim()` and `updatePreviewParticles()` on the next editor visibility frame;
- does not force the normal 70-alpha editor selection policy over ordinary visible objects while Preview Mode is enabled;
- keeps editor-only background suppression only after the OFF transition has had time to settle.

## Preserved behavior

The following tweaks11/12 fixes are intentionally retained:

- 2019-vs-2022/2023 editor input ABI split;
- separate GameManager gameplay and editor layer slots;
- APK-derived ground/background texture limits;
- null batch-texture guards;
- x86 hotkey visibility guard during editor Play;
- wrapper-internal Windows audio volume handling.

`src/shared/audio_win.c` is byte-for-byte identical to tweaks12 because the user confirmed the 1.0 music fix is working.

## Runtime boundary

The source was not executed as a Windows wrapper in this environment. The uploaded logs, exact ARM ELF disassembly, source/static audit, patch reconstruction, and package-integrity checks can be verified here; actual gameplay/editor behavior still needs the user's Windows test build.
