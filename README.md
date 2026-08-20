# Geometry Dash Wrapper 0.9.6-gdpstweaks7

Cross-version Windows wrapper for Geometry Dash Android builds.

## Current tweaks7 changes

- Fixes the wrapper-owned stock 2.2 beta editor initialization path by calling
  each APK's real `GJBaseGameLayer::init()` before editor setup.
- Keeps separate 2019, 2022 and 2023 ARMv7 editor-layout profiles.
- Enables pause-button removal and gameplay cursor hiding by default.
- Applies pause suppression before guest rendering and reasserts it continuously
  on x86/legacy ARM when enabled. Escape pause remains available.

## Default desktop settings

```bat
set "REMOVE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_WHEN_PLAYING=true"
```

Set either variable to `false` to restore the native behavior.

See `GDPSTWEAKS7.md` for details and prior `GDPSTWEAKS*.md` files for branch
history.
