# Geometry Dash Wrapper 0.9.5 — EnduranceTest4

`endurancetest` is the cross-version stability branch. EnduranceTest4 removes
the pause-button and cursor experiments completely, adds the original PC
Practice Mode Z/X controls, fixes editor-session overlay reset, targets the
2.2 right-side black region, and repairs legacy MCI music-volume replay.

## Running

Python is not required.

- `RUN_AUTO_GDPS.cmd` uses `naskogdps17.7m.pl/database`.
- `RUN_AUTO_BOOMLINGS.cmd` uses `www.boomlings.com/database`.

Double-click a launcher to use `game.apk`, or drag an APK onto either launcher.
The native launcher selects the backend and creates the package/version/backend
save profile and dated log folder.

## Default setting

```bat
set "VERSION_ISOLATED_SAVES=true"
```

Version-isolated saves remain toggleable and enabled by default. Setting
`VERSION_ISOLATED_SAVES=false` restores the old flat `save\` root. Existing
flat saves are not guessed or migrated automatically.

## EnduranceTest4 changes

- Removes all wrapper cursor hiding on x86, legacy ARM and ARMv7. The Windows
  cursor is no longer modified by gameplay, pause, Resume or editor state.
- Removes all wrapper pause-button hit blocking and visual hiding. The APK's
  top-right pause control is native again; Escape still calls Android Back.
- Adds Z to place and X to remove Practice Mode checkpoints by calling the
  APK's own `UILayer::onCheck` and `UILayer::onDeleteCheck` callbacks.
- Reapplies the stored music volume after MCI play/resume/seek operations,
  targeting the 1.0–1.4 first-attempt/later-attempt volume mismatch.
- Resets song-guide/BPM lifecycle counters whenever a different 2.2 editor
  layer appears, so reopening the editor starts the overlay refresh at frame 1.
- Clears stale OpenGL scissor state and restores the native viewport before
  2.2 editor frames. This narrowly targets the moving right-side black region.
- Keeps EnduranceTest3's x86 pacing, account backup transport, save isolation,
  network handling and object-colour repair. No x86 optimisation was attempted.
- Contains no Python files and no `.gitignore`.

## Runtime confirmation needed

The black-region repair, repeated editor reopening, legacy volume consistency
and Z/X callbacks still require a Windows/APK test. The editor death-X colour
was not changed in this build because the uploaded logs do not establish the
beta's intended colour.

## Code guide

Start with `docs\CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`.
