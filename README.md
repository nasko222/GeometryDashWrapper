# Geometry Dash Wrapper 0.9.5 — EnduranceTest2

`endurancetest` is the long-session and cross-version regression branch.
EnduranceTest2 is a narrow correction over EnduranceTest1: it removes the
freezing 2.2 editor experiment, repairs the old-client backup send state, and
makes x86 cursor visibility follow the actual pause scene. The accepted Fix6
x86 frame-pacing implementation is unchanged.

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

```bat
set "DISABLE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_DURING_PLAY=true"
set "VERSION_ISOLATED_SAVES=true"
set "V22_EXACT_EDITOR_VISIBILITY=false"
```

`VERSION_ISOLATED_SAVES=true` stores all game state and caches by package,
version and backend, for example:

```text
save\com.robtopx.geometryjump__v1.93__x86\
save\com.robtopx.geometryjump__v2.111__x86\
save\com.robtopx.geometrydashsubzero__v2.2.12__armv7\
```

Set it to `false` to use the old shared `save\` directory. Existing flat saves
are left untouched and are never guessed or migrated automatically.

`V22_EXACT_EDITOR_VISIBILITY` remains visible only for comparison with old
logs. The exact companion redirect is hard-disabled in this endurance build
because it caused multi-second editor frames and freezes. The faster Fix6 host
visual path is always used.

## EnduranceTest2 changes

- Keeps the accepted Fix6 x86 pacing function unchanged.
- Walks the live Cocos scene graph on x86 so the cursor stays visible in pause
  and options layers, then hides again after Resume.
- Keeps the top-right pause-button behavior and Escape pause behavior unchanged.
- Preserves a synthetic API socket's logical connected state after
  `HTTP/1.1 100 Continue`, reports `POLLOUT`, and joins a following backup body
  whether the old client sends it through `send` or `writev`.
- Hard-disables the exact 2.2 companion visibility redirect that froze the
  editor. The confirmed Fix6 object-color repair remains active.
- Keeps version-isolated saves, native launch, APK drag-and-drop, dated logs,
  clean titles, supplied icon families, and separate Boomlings/GDPS launchers.
- Contains no Python files and no `.gitignore`.

## Open editor issues

The song-position line, BPM guidelines and moving right-side black region are
not claimed fixed in this build. Disassembly showed that the freezing companion
visibility routine never called `DrawGridLayer::updateTimeMarkers`, so it was
the wrong path for those overlays. EnduranceTest2 removes the freeze first.

## Readability

Start with `docs/CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`. New code uses named
helpers and comments that compare Win32/C concepts with C#/Java concepts. The
established emulator cores are not cosmetically rewritten during an endurance
branch because that would add unrelated regression risk.
