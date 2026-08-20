# Geometry Dash Wrapper 0.9.6-gdpstweaks4

Focused desktop-display and gameplay-UI tweak pass over gdpstweaks3.

## Changes

- Windows DPI scaling is now handled by the wrapper itself. Every Windows process opts into Per-Monitor-V2 DPI awareness before creating a window, with older-Windows fallbacks. This replaces the need to set Compatibility > High DPI scaling override > Application manually.
- Reverted gdpstweaks3's forced GL_LINEAR texture-magnification workaround. The game owns its texture filtering again; DPI awareness fixes the actual Windows scaling problem.
- Added `REMOVE_PAUSE_BUTTON`, default `false`, to both RUN_AUTO batch files and shared runtime settings.
  - x86 derives the pause-item field from each APK's `UILayer::init` and uses the game's `CCNode::setVisible(false)`.
  - legacy ARM uses the previously verified legacy UILayer pause-control path.
  - the selected 2.2 ARMv7 beta uses its audited UILayer pause member and the game's `CCNode::setVisible(false)`.
  - the top-right touch target is blocked only during player gameplay; Escape pause is preserved.
- Added `HIDE_CURSOR_WHEN_PLAYING`, default `false`, to both RUN_AUTO batch files and shared runtime settings.
  - cursor hiding is limited to player gameplay; editor/text input/inactive windows keep it visible.
  - Escape immediately makes the cursor visible for pause interaction.
- Extras remains disabled.

## Defaults

```bat
set "REMOVE_PAUSE_BUTTON=false"
set "HIDE_CURSOR_WHEN_PLAYING=false"
```

Set either value to `true` in the RUN_AUTO CMD to enable it.
