# Geometry Dash Wrapper 0.9.6-gdpstweaks5

## Goal

Run the editor in recognized **stock/original ARMv7 Geometry Dash 2.2 beta APKs** without requiring a modded APK or `libgame.so`.

## Recognized stock profiles

| Profile | Primary ARMv7 library | Editor ABI | Stock state |
| --- | ---: | --- | --- |
| 2019 early beta | 9,144,004 bytes | `LevelEditorLayer::init(GJGameLevel*)` | init stubbed, visibility real |
| 2022 Lite 2.2.11-era | 9,541,500 bytes | `LevelEditorLayer::init(GJGameLevel*, bool)` | init + visibility stubbed |
| 2023 SubZero 2.2.12-era | 9,578,364 bytes | `LevelEditorLayer::init(GJGameLevel*, bool)` | init + visibility stubbed |

Recognition also validates the expected editor symbols and confirms the primary initializer is the tiny stub. Exact file size alone is not enough to enter the restoration path.

## Wrapper-owned restoration

The ARMv7 backend now performs the missing editor setup in guest memory using functions exported by the stock primary library. It does not copy or load the donor mod implementation.

The restoration initializes retained arrays/dictionaries and section vectors, installs the active level/editor references, calls `setupLayers`, creates the grid and players, decompresses the level setup, calls `createObjectsFromSetup`, creates text layers and `EditorUI`, restores ground/background state, and initializes editor counters/mode/groups.

2019 uses its older player and editor ABI. 2022 and 2023 use separate late layouts. The late stock builds also receive the existing host editor-visibility bridge because their native `LevelEditorLayer::updateVisibility(float)` is only a stub.

## Safety boundary

- No APK, donor `libgame.so`, extracted primary library, EXE, or DLL is included.
- Unknown 2.2 beta layouts do not receive stock-layout field writes.
- Companion-wide constructors/`ApplyHooks`, DPAD hooks, timer hooks, shader hooks, collision hooks, and GDPS-menu modifications are not part of stock restoration.
- The 1.0-1.4 legacy backend and x86 backend are otherwise unchanged by this editor work.

## Runtime status

The ARMv7 source passes a C++20 syntax check in this environment and each known stock library exposes the symbols required by its profile. Full Windows/Dynarmic gameplay execution is not available here, so 2019/2022/2023 editor entry, editing, save/exit, and playtest still require runtime confirmation.
