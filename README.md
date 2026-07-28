# Geometry Dash Wrapper 0.9.5-unified3

Unified3 keeps the three proven execution cores and fixes the launch/network/
visual issues found while testing Unified2:

- **x86 native, first priority:** `0.9.3-alpha3`
- **legacy ARM/Thumb through Dynarmic:** `dynarmictest14-fix1`
- **ARMv7 / 2.2 through Dynarmic:** `0.9.4-milestone1`

There is no Unicorn backend or dependency. F2 remains removed. All backends use
one `dist-unified/save/` directory.

## Launch settings

Edit the values at the top of `RUN_AUTO.cmd` before running:

```bat
set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "MUSIC_PULSE_MAX=0.30"
set "OVERRIDE_ARM=false"
```

- `GDPS_SERVER` routes levels, accounts, comments, gauntlets and other game API
  calls while preserving the complete relative endpoint path. For example,
  `/database/accounts/loginGJAccount.php` remains under `/accounts/`.
- `getGJSongInfo.php` first uses the configured GDPS, allowing private custom
  song catalogues. If that endpoint is missing, returns `-1`, or emits HTML/PHP
  proxy warnings, the wrapper retries official HTTPS Boomlings. The returned
  CDN/song URL itself is never rewritten.
- `HACK_ICONS=true` makes supported icon ownership checks return unlocked for
  that run without writing fake unlocks to the save.
- `FULL_BYPASS=true` applies only hooks that exist in the selected game. It
  redirects the spin-off Full Version button through the normal Creator path,
  enables `canPlayOnlineLevels`, and redirects the locked editor callback to My
  Levels. Missing or differently compiled exports are skipped rather than
  aborting startup. This is intended to unlock the editor, recent/search and
  other Creator buttons in GD World/Lite-style builds.
- `MUSIC_PULSE_MAX` caps the DSP value used by music-reactive/rave visuals. It
  does not lower audio volume. Lower it (for example `0.20`) for weaker pulses.
- `OVERRIDE_ARM=false` keeps x86 first. `true` prefers ARMv7, then legacy ARM,
  when the APK contains multiple architectures.

## Unified3 fixes

- Centers x86, legacy ARM and ARMv7 windows on the primary screen.
- Removes the exact-prologue requirement that made GD Lite fail with
  `expected 0xe368 got 0xb510` when Full Bypass was enabled.
- Preserves nested GDPS endpoint paths, fixing account-login URLs.
- Uses custom-song-first metadata routing with automatic official Boomlings
  fallback for broken GDPS proxy scripts.
- Delivers at most one completed ARMv7 HTTP callback per frame and gives large
  level responses an adaptive callback budget, preventing the fixed
  one-billion-tick crash seen while loading a gauntlet level under load.
- Resets leaked OpenGL scissor/viewport/write state before every ARMv7 frame to
  prevent stale black strips after editor playtest/scene transitions.
- Maps and smooths the Windows audio meter into the configurable pulse cap so a
  single loud peak cannot create screen-sized rave objects.

## Build and run

Run `BUILD_ALL.cmd`. Put the APK at `dist-unified/game.apk`, then run
`dist-unified/RUN_AUTO.cmd`.

Automatic priority is x86, legacy ARM, then ARMv7 unless `OVERRIDE_ARM=true`.
`RUN_DEBUG.cmd` remains available under each ARM backend directory for focused
logs/profiles.

Back up `dist-unified/save/` before switching between game versions that reuse
the same save filenames.
