# Geometry Dash Wrapper 0.9.5 — EnduranceTest5

`endurancetest` is the cross-version stability branch. EnduranceTest5 is a
regression correction over EnduranceTest4. It makes Z/X strictly Practice
Mode-only, repairs the legacy callback ABI, removes the destructive editor grid
refresh and rolls back the unsuccessful OpenGL clip-state experiment.

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

Version-isolated saves remain toggleable and enabled by default. Existing flat
saves are not guessed or migrated automatically.

## EnduranceTest5 changes

- **Z/X are now fail-closed and Practice Mode-only.**
  - Selected ARMv7 beta: checks the exact `PlayLayer + 0x29A0` flag derived
    from that APK's `PlayLayer::togglePracticeMode(bool)` implementation.
  - x86: derives the flag offset from each APK's own
    `PlayLayer::togglePracticeMode(bool)` compare/store instructions.
  - legacy ARM 1.0–1.4: calls the exported
    `PlayLayer::getPracticeMode()` accessor before any checkpoint callback.
- Legacy ARM now resolves both old no-argument checkpoint callbacks and newer
  sender-argument callbacks. It also uses `PlayLayer::getUILayer()` when
  available, fixing Z/X doing nothing on 1.0–1.4.
- Removes every host call to `LevelEditorLayer::updateGridLayer`. The uploaded
  log showed the guessed playtest flag alternating between 0 and 1 repeatedly;
  those calls rebuilt the grid and could erase placed-object visuals and BPM
  guides.
- Keeps the per-frame song-position guide, periodic
  `DrawGridLayer::updateTimeMarkers`, and editor-session reset.
- Rolls back EnduranceTest4's editor scissor/viewport sanitizer. The guest again
  owns all editor clip state. The right-side black region remains open rather
  than being falsely claimed fixed.
- Legacy MCI volume is reasserted for 45 rendered frames after play, resume,
  rewind or seek/replay. This targets MCI restoring device-default volume after
  the immediate command has already returned.
- Keeps the accepted x86 pacing, networking, backups and save isolation
  unchanged. No x86 optimisation was attempted.
- Mouse hiding and pause-button hiding remain completely removed.
- Contains no Python files and no `.gitignore`.

## Runtime confirmation needed

Windows/APK testing is still required for legacy attempt-to-attempt volume,
Practice Mode Z/X on every supported version, and editor BPM/object stability.
The moving right-side black region is **not claimed fixed** in this build.

## Code guide

Start with `docs\CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`.
