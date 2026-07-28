# Geometry Dash Wrapper 0.9.5-unified2-fix2

Unified2 Fix2 repairs the two regressions introduced by the configurable launch
hooks:

- spin-off full-version bypass now follows the game's normal CreatorLayer scene
  path instead of jumping directly into My Levels, which could leave the editor
  background and ground uninitialized as a gray void;
- custom-song metadata always uses official HTTPS Boomlings, independently of
  the configured gameplay/GDPS server. The song CDN URL returned by Boomlings
  remains untouched.

The three execution lines remain:

- **x86 native, default highest priority:** `0.9.3-alpha3`
- **legacy ARM/Thumb through Dynarmic:** `0.9.4-arm-dynarmictest14-fix1`
- **ARMv7 / Geometry Dash 2.2 through Dynarmic:** `0.9.4-milestone1`

There is no Unicorn backend or dependency.

## Editable launch settings

Build the project, then edit the four values at the top of
`dist-unified/RUN_AUTO.cmd`:

```bat
set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "OVERRIDE_ARM=false"
```

- `GDPS_SERVER` changes the Geometry Dash PHP API base for levels, accounts,
  comments, gauntlets and similar game services. It accepts `host/path`,
  `http://host/path`, or `https://host/path`.
- `HACK_ICONS=true` makes supported icon-ownership checks return unlocked for
  that run. Color ownership and save data are not modified.
- `FULL_BYPASS=true` redirects the spin-off main-menu Full Version button to the
  normal Creator button handler. It no longer patches CreatorLayer gates or
  jumps directly into My Levels.
- `OVERRIDE_ARM=true` chooses ARMv7, then legacy ARM, when the APK also contains
  x86. With `false`, x86 remains first priority.

`getGJSongInfo.php` is deliberately excluded from GDPS routing and is sent to:

```text
https://www.boomlings.com/database/getGJSongInfo.php
```

The returned song/CDN download URL is not rewritten.

## Automatic backend selection

Put one APK at `dist-unified/game.apk` and run `dist-unified/RUN_AUTO.cmd`, or
pass an APK path to it.

Normal priority:

1. x86: `lib/x86/libcocos2dcpp.so` or `lib/x86/libgame.so`
2. legacy ARM: `lib/armeabi/libgame.so`
3. ARMv7/2.2: `lib/armeabi-v7a/libcocos2dcpp.so`

## One save folder

Every backend runs from `dist-unified/` and uses:

```text
dist-unified/save/
```

Back up this folder before switching between substantially different game
versions because some versions reuse the same save filenames.

## Build

- `BUILD_X86.cmd` builds x86.
- `BUILD_DYNARMIC.cmd` builds both Dynarmic backends.
- `BUILD_ALL.cmd` builds all three and creates the automatic launcher.

Normal ARM launchers avoid heavy tracing. Each ARM backend retains a separate
`RUN_DEBUG.cmd`. The old F2 editor shortcut remains fully removed.
