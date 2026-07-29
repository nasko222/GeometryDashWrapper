# Geometry Dash Wrapper 0.9.5 — EnduranceTest1

`endurancetest1` is a correctness and long-session regression branch. It keeps
the Fix6 x86 frame-pacing implementation unchanged and focuses on failures that
appeared while moving repeatedly between Geometry Dash versions.

## Running

Python is not required to build or run the wrapper.

Use either launcher:

- `RUN_AUTO_GDPS.cmd` — `naskogdps17.7m.pl/database`
- `RUN_AUTO_BOOMLINGS.cmd` — `www.boomlings.com/database`

Double-click a launcher to use `game.apk`, or drag an APK onto it. The native
`GeometryDashLauncher.exe` reads the APK package and version, selects the
backend, applies the game title/icon, chooses the save profile and creates a
dated log folder.

## Default launch settings

Both launchers expose these switches:

```bat
set "DISABLE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_DURING_PLAY=true"
set "VERSION_ISOLATED_SAVES=true"
set "V22_EXACT_EDITOR_VISIBILITY=true"
```

`VERSION_ISOLATED_SAVES=true` stores data by package, version and backend:

```text
save\com.robtopx.geometryjump__v1.93__x86\
save\com.robtopx.geometryjump__v2.111__x86\
save\com.robtopx.geometrydashsubzero__v2.2.12__armv7\
```

Set it to `false` to use the old shared `save\` directory. Existing flat saves
are left untouched and are never guessed or migrated automatically.

`V22_EXACT_EDITOR_VISIBILITY=true` selects the late-beta companion's complete
editor visibility routine. It is a correctness-first option and can cost more
CPU than the Fix6 approximation. Set it to `false` to return to the Fix6 host
approximation.

## EnduranceTest1 changes

- Leaves the accepted Fix6 x86 pacing function byte-for-byte unchanged.
- Keeps the cursor visible after Escape while a paused PlayLayer remains alive.
  Definite gameplay keyboard input hides it again.
- Hides the late-beta top-right pause item itself while preserving Escape and
  the complete pause menu. The x86 path uses RTTI validation and is best-effort
  across releases; legacy ARM keeps its safe top-right click block.
- Adds an HTTP `100 Continue` compatibility reply for large legacy account/save
  POSTs. This targets the 1.93 backup case where the client connected but never
  transmitted a complete request to the GDPS.
- Uses the supplied late-beta companion's exact `LevelEditorLayerExt::
  updateVisibilityH` path by default, wiring its original-function slot to the
  primary game implementation. This targets the missing song-position line,
  missing BPM guidelines and stale right-side editor region while retaining
  the object-colour repair already confirmed in Fix6.
- Keeps version-isolated saves enabled by default.
- Keeps native launch, APK drag-and-drop, dated logs, clean titles, supplied
  icon families and separate Boomlings/GDPS launchers.
- Contains no Python files and no `.gitignore`.

## Readability

Start with `docs/CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`. New code uses named
helpers and comments that compare Win32/C concepts with C#/Java concepts. The
established emulator cores are not cosmetically rewritten during an endurance
branch because that would add unrelated regression risk.
