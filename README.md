# Geometry Dash Wrapper 0.9.6-gdpsfixes4

`gdpsfixes4` keeps the Android-only GDPSFixes line and adds wrapper-level launch,
editor, and Extras controls across the x86, legacy ARM, and ARMv7 backends.
The abandoned iOS backend remains removed.

## New toggles

The normal RUN batch files set all three toggles to `true`:

- `I_LOST_THE_GAME=true` — mandatory joke launch gate. The launcher and every
  backend default this setting to false and refuse direct execution when it is
  not true.
- `EDITOR_CONTROLLS=true` — enables host keyboard shortcuts in the level editor.
- `EXTRAS_MENU=true` — enables a small native Extras button over menu screens.

The environment variable name `EDITOR_CONTROLLS` intentionally keeps the
requested spelling.

## Editor controls

When `EDITOR_CONTROLLS=true` and an EditorUI is active:

- `W` / `A` / `S` / `D` — move the selection one editor step.
- `Shift+W/A/S/D` — use the corresponding larger move command.
- `Q` — rotate counter-clockwise.
- `E` — rotate clockwise.

The wrapper does not send these shortcuts through `EditorUI::keyDown`. Movement
uses the game's own `EditorUI::moveObjectCall` callback and rotation uses
`EditorUI::transformObjectCall`. Newer ARMv7 builds use the exported EditCommand
overloads directly; older/x86-era builds can use the original sender/tag ABI and
restore the original sender tag after the call. Rotation command values are kept
backend-aware: old/x86-era editors use 11/12, while the audited 2.2 ARMv7 editor
uses 0x13/0x14.

## Extras menu

When `EXTRAS_MENU=true`, the wrapper adds an `Extras` button to the host game
window while gameplay/editor activity is not detected. The popup is available
on all wrapper backends, with version-specific actions where supported.

For full Geometry Dash versions beginning with 1.0, 1.1, 1.2, or 1.3:

- **Play Placeholder Level** — constructs the game's raw default `GJGameLevel`
  and enters it through the original `PlayLayer::scene` path. Static auditing of
  the supplied GD 1.0 binary confirms LevelTools ID 0 is not a placeholder; it
  aliases level 1. Background music is suppressed for the raw placeholder run.

For full Geometry Dash version **1.02** only:

- **Play Time Machine Beta** — obtains built-in level ID 8 through the game's
  original `LevelTools::getLevel(8)` path and opens it normally.

No APK data is bundled with these features.

## GDPSFixes3 build repair

The GDPSFixes3 ARMv7 source accidentally called a legacy-only
`ApkMemberCache::LocateIndex` helper and failed to compile. GDPSFixes4 removes
that call and uses the ARMv7 cache's real `Exists()` API for extension-resource
existence checks.

The GDPSFixes3 color-picker compatibility changes remain present; they still
need a live gameplay retest because the fixes3 package could not complete the
user's full build.

## Carried fixes

- Large ARM/GDPS level uploads are no longer chopped at 4095 formatted bytes.
- MCI music seeks stop the alias before seeking for Wine/Proton compatibility.
- Legacy ARM nonblocking sockets no longer synchronously stall on bad network.
- Legacy color-picker extension-resource mirroring/fallback from GDPSFixes3 is
  retained.

## Building

Run `BUILD_ALL.cmd` on Windows. No APK, extracted proprietary game library,
built game executable, or iOS backend is included in the source archive.
