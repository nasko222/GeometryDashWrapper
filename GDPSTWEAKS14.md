# Geometry Dash Wrapper 0.9.6-gdpstweaks14

This is a narrow correction to the failed tweaks13 runtime test. It intentionally does not change the confirmed 1.0 audio implementation or make another speculative Preview Mode change.

## ARMv7 2.2 editor

The tweaks13 CCArray validator was wrong. The exact shipped 2019/2022/2023 binaries confirm the CCArray object stores its native ccArray pointer at +0x20 for 2019 and +0x30 for 2022/2023. Exact `CCArray::objectAtIndex` disassembly also proves the inner native layout differs by family: 2019 uses a 12-byte `ccArray` with `CCObject**` at +0x08, while 2022/2023 use a 16-byte layout with `CCObject**` at +0x0C. Tweaks13 incorrectly applied the 2019 +0x08 rule to every profile, so both late betas falsely rejected every freshly created CCArray and aborted editor initialization with `V22 wrapper editor CCArray returned an uninitialized native CCArray`. Tweaks14 validates both object and inner-array layouts by profile.

No Preview Mode code is changed in this build because the new runtime logs never reached a usable restored editor; Preview cannot be meaningfully retested until editor initialization succeeds.

## x86 editor controls

Tweaks12/13 made running-scene detection a hard gate and disabled the controls on the user's real 1.50 x86 APK. Tweaks14 restores the tweaks11 discovery behavior, including the LevelEditorLayer fallback pass that was present in the last user-confirmed working editor-controls build. The speculative derived CCDirector scene-field shortcut is no longer preferred.

To prevent the old menu lag from returning, a failed EditorUI discovery is cached for the current CCScene. Repeated A/D/W/S/Q/E presses in the same non-editor scene return immediately after the first miss instead of recursively scanning up to thousands of nodes every time. A scene change automatically permits a new discovery pass. Hidden EditorUI remains blocked during editor Play.

## Unchanged

- Shared Windows audio source / 1.0 music fix.
- Tweaks12 dual GameManager editor/game-layer slots.
- Tweaks11 APK-derived art limits.
- Tweaks13 Preview transition implementation (pending a runtime test after editor startup is repaired).
