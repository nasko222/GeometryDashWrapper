# Geometry Dash Wrapper 0.9.6-gdpstweaks8

Cross-version Windows wrapper for Geometry Dash Android builds.

## Current tweaks8 changes

- Keeps `REMOVE_PAUSE_BUTTON=true` and `HIDE_CURSOR_WHEN_PLAYING=true` as the default desktop behavior.
- ARMv7 pause removal now suppresses the pause item at its exact `UILayer::init` creation call, before the item can render; Escape pause remains available.
- Corrects stock 2.2 beta editor restoration for the supplied 2019, 2022 and 2023 layouts: exact setup fields, strict host setup decoding, capacity-only late editor vectors, and 2019 editor sprite-atlas loading.
- Keeps the previous FULL_BYPASS CreatorLayer correction and the separate per-version editor profiles.

## Default desktop settings

```bat
set "REMOVE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_WHEN_PLAYING=true"
```

Set either variable to `false` to restore the native behavior.

See `GDPSTWEAKS8.md` for the new changes and prior `GDPSTWEAKS*.md` files for branch history.
