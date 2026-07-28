# Geometry Dash Wrapper 0.9.5 — Unified7 Fix5 Stabilization

One source tree for Android Geometry Dash builds using:

- the native x86 backend;
- legacy ARM/Thumb through Dynarmic;
- ARMv7/2.2 beta through Dynarmic.

## Running

Python is not required to build or run the wrapper.

Use either launcher:

- `RUN_AUTO_GDPS.cmd` — `naskogdps17.7m.pl/database`
- `RUN_AUTO_BOOMLINGS.cmd` — `www.boomlings.com/database`

Double-click a launcher to use `game.apk`, or drag an APK onto it. The native
`GeometryDashLauncher.exe` reads the APK package and version, selects the
correct backend, applies the game title/icon, selects the matching save profile,
and creates a dated log folder.

## Save profiles

Local data is isolated by Android package, game version and backend:

```text
save\com.robtopx.geometryjump__v1.93__x86\
save\com.robtopx.geometryjump__v2.111__x86\
save\com.robtopx.geometrydashsubzero__v2.2.12__armv7\
```

`CCGameManager.dat`, `CCLocalLevels.dat`, `preferences.bin`, audio cache and APK
member cache therefore stay with the exact client that created them. Existing
files directly under the old flat `save` folder are deliberately left untouched
and are not guessed/migrated into a version profile.

This protects startup from incompatible local saves. It cannot make cloud data
from a newer GD version parse safely in an older client.

## Fix5 changes

- Restores the ARMv7 guest shader source byte-for-byte. The removed desktop
  sanitizer deleted required GLES precision declarations and made the 2.2 beta
  abort during `nativeInit`.
- Selects x86 API socket behavior by game version. 2.11 uses the older real
  nonblocking/pending connection flow; 2.0/2.1 and older clients retain the
  synthetic-ready bridge that currently works for them.
- Removes the extra `Sleep(1)` after a successful x86 vsynced `SwapBuffers`.
  Sleeping after the vblank wait caused uneven frame pacing despite nominal FPS.
- Adds PC-style gameplay options, enabled in both BAT files:
  `DISABLE_PAUSE_BUTTON=true` and `HIDE_CURSOR_DURING_PLAY=true`.
  The pause-button callback is suppressed on supported backends. Exact
  play-layer-aware cursor hiding is implemented by the native x86 backend.
- Keeps clean window titles from creation onward: Geometry Dash, Lite, World,
  Meltdown or SubZero—never the backend name.
- Keeps the native launcher, APK drag-and-drop, dated logs, and no-Python build.
- Contains no `.gitignore` file.

## Icons

The existing icon set is unchanged in this release. Custom square transparent
PNGs can be supplied in a later icon-only pass without mixing icon work into
runtime stabilization.

## Readability

Start with `docs/CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`. New logic is split into
small named helpers and includes C#/Java-oriented comments. The emulation cores
are not mass-refactored cosmetically because that would add regression risk.
