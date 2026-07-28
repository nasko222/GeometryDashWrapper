# Geometry Dash Wrapper 0.9.5-unified5

Unified wrapper for x86 Android game libraries, legacy ARM through Dynarmic,
and ARMv7/2.2 through Dynarmic. There is no Unicorn backend.

## Run settings

Edit `RUN_AUTO.cmd`:

```bat
set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "FORCE_HIGHEST_GRAPHICS=true"
set "MUSIC_PULSE_MAX=0.30"
```

x86 is always preferred when present. The old ARM override setting was removed.

## Unified5 changes

- Stronger editor-playtest edge crop correction for both default and full-window
  offscreen framebuffers.
- Geometry Dash World keeps each Creator button's native destination and normal
  enabled appearance instead of routing everything to My Levels.
- 2.11 x86 forces CCDirector's high texture-quality path in addition to HD and
  low-memory capability checks.
- Window title is exactly Geometry Dash, Geometry Dash World, Geometry Dash
  Meltdown, or Geometry Dash SubZero according to the APK package.
- APK icons are packed as a multi-size ICO so the 16px title-bar icon stays sharp.
- Saves remain under one `save` root but are isolated in
  `save/profiles/<android-package>/` to prevent a 2.2 beta save from crashing 2.11.
- 1.6 x86 networking and the working platformer and icon fixes are retained.

An optional `icon.png` beside the launcher still overrides the APK icon.
