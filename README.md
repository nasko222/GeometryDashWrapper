# Geometry Dash Wrapper 0.9.6-gdpstweaks9

## gdpstweaks9

- ARMv7 stock 2.2 editor restoration now supplies safe frame-cache aliases for editor-only sprite frames omitted from the reduced Lite/SubZero APKs. No donor APK assets are bundled.
- ARMv7 pause removal hooks the `UILayer` virtual `init()` path and hides the exact stored pause item before the layer can be presented.
- x86 recurring pause/cursor detection no longer performs a recursive Cocos scene walk every polling interval, removing the periodic level-selector hitch seen in 1.5/1.6.
- x86 pause removal also attempts creation-time suppression of the discovered `UILayer` pause item, with the existing runtime hide retained as fallback.

Cross-version Windows wrapper for Geometry Dash Android builds.

## Current tweaks9 changes

- Keeps `REMOVE_PAUSE_BUTTON=true` and `HIDE_CURSOR_WHEN_PLAYING=true` as the default desktop behavior.
- ARMv7 stock 2019/2022/2023 editor restoration aliases the currently identified missing editor frame-cache names to an existing stock frame; no modded-APK assets are bundled.
- ARMv7 pause removal hides the exact stored pause item from a `UILayer::init()` vtable thunk before the layer can be presented.
- x86 recurrent pause/cursor detection no longer recursively scans the complete Cocos scene tree, targeting the periodic 1.5/1.6 level-selector hitch.
- x86 pause removal attempts to hide the discovered pause item at its `CCMenuItemSpriteExtra::create` call, retaining the previous runtime hide as fallback.
- Keeps tweaks8's strict/fail-closed stock editor setup decoding and the previous FULL_BYPASS CreatorLayer corrections.

## Default desktop settings

```bat
set "REMOVE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_WHEN_PLAYING=true"
```

Set either variable to `false` to restore the native behavior.

See `GDPSTWEAKS9.md` for the new changes and prior `GDPSTWEAKS*.md` files for branch history.
