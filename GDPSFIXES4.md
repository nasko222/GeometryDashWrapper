# GDPSFixes4 notes

Branch: `gdpsfixes4`
Version: `0.9.6-gdpsfixes4`

## Build repair

GDPSFixes3 did not complete a full Windows build because the ARMv7 backend
referenced `ApkMemberCache::LocateIndex`, a helper that exists only in the
legacy ARM cache implementation. The ARMv7 extension fallback now uses its
native `Exists()` method and contains no `LocateIndex` reference.

## Launch gate

`I_LOST_THE_GAME` defaults to false. The native launcher, x86 backend, legacy
ARM backend, and ARMv7 backend each refuse to start unless it is true. The
normal RUN batch files and generated backend RUN/RUN_DEBUG files set it true.

## Editor controls

`EDITOR_CONTROLLS` defaults to false. The normal RUN batch files enable it.
The host key mapping is:

- A/D/W/S -> move commands 1/2/3/4
- Shift+A/D/W/S -> move commands 5/6/7/8
- E/Q -> rotate clockwise/counter-clockwise

The implementation deliberately avoids `EditorUI::keyDown`. Movement uses the
game's real `EditorUI::moveObjectCall` path and rotation uses
`EditorUI::transformObjectCall`. Old/x86-era editors use rotation tags 11/12;
the ARMv7 2.2 editor uses its verified EditCommand values 0x13/0x14.

## Extras

`EXTRAS_MENU` defaults to false. The normal RUN batch files enable it. A native
Extras button is attached to each wrapper window and hidden while gameplay or
an editor is detected.

Full Geometry Dash 1.0x-1.3x gets `Play Placeholder Level`. The early binary's
`LevelTools::getLevel(0)` path was disassembled and found to alias level 1, so
the wrapper instead creates a raw default `GJGameLevel`, passes it to
`PlayLayer::scene`, and suppresses background music for that placeholder run.

Full Geometry Dash 1.02 additionally gets `Play Time Machine Beta`, which uses
`LevelTools::getLevel(8)` and the normal PlayLayer scene path.

## Runtime test status

The current environment does not contain the project's pinned Windows Zig /
Dynarmic toolchain, so a full Windows link and live gameplay test cannot be
performed here. Static source checks, focused C compilation, real-APK symbol
audits, patch application, and tree reconstruction are used instead.
