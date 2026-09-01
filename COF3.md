# Geometry Dash Wrapper 0.9.7-cof3

COF3 is a focused feature branch over `0.9.7-cof2`.

## Geometry Dash 1.02 native comments hotkey

The ARM-legacy backend exposes the comments browser that already exists in the
original Geometry Dash 1.02 native library.

While the normal `LevelInfoLayer` level screen is open, press **C**. The wrapper
finds the active `LevelInfoLayer` and calls the game's own exported
`LevelInfoLayer::onInfo()` method. That native method creates and shows the
original `InfoLayer`, so the wrapper does not recreate the comments UI.

The hotkey is intentionally restricted to the **1.02 release generation**, without
comparing `GD_GAME_VERSION` to the literal text `"1.02"`. The native launcher
exports the manifest `versionCode`; ARM-legacy accepts versionCode **4** for the
`com.robtopx.geometryjump` and `com.robtopx.geometryjumplite` package families,
and then additionally requires the native 1.02 comment capability symbols
(`LevelInfoLayer::onInfo`, `InfoLayer::create`, `getLevelComments`, and
`uploadComment`). The result is cached into the window host before `C` is
intercepted, and the capability is checked again immediately before guest
dispatch. 1.0 and 1.1+ therefore do not get the hotkey.

The existing GDPS URL rewrite remains responsible for requests such as
`getGJComments.php` and `uploadGJComment.php`. COF3 does not add a dislike UI.

The supplied 1.02 APK was checked directly:

- package: `com.robtopx.geometryjump`
- versionName: `1.02`
- versionCode: `4`
- `lib/armeabi/libgame.so`: 4,093,996 bytes
- exports `_ZN14LevelInfoLayer6onInfoEv`
- the function reads the level from `LevelInfoLayer + 0x150`, calls
  `InfoLayer::create(GJGameLevel*)`, then invokes the returned layer's `show()`.

## FPS setting

COF3 adds one wrapper setting shared by all three backends:

```bat
set "FPS=VSYNC"
```

`VSYNC` is the default. It requests OpenGL swap interval 1 and does not apply a
numeric host cap.

A numeric value disables swap interval synchronization and enables a
high-resolution host frame limiter. Examples:

```bat
set "FPS=144"
set "FPS=240"
set "FPS=9999"
```

Valid numeric values are 1 through 10000. Missing, malformed, zero, negative,
or out-of-range values safely fall back to `VSYNC`.

The cap uses `QueryPerformanceCounter` with a deadline scheduler, a 1 ms Windows
timer period for coarse sleeps, and thread yielding for the final sub-2 ms
portion. This is used by x86, ARM-legacy and ARMv7. If swap-control is
unavailable in VSYNC mode, the wrapper falls back to a 60 FPS host cap rather
than running unbounded.

## Preserved behavior

COF2 ARMv7 regression fixes remain present, including the late-2023 art bounds
clamp and Endurance platformer/editor routing. The retired stock 2.2 editor
reconstruction remains retired. Existing x86, ARM-legacy, audio, GDPS, save,
fullscreen and desktop compatibility work is otherwise retained.
