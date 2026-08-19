# Geometry Dash Wrapper 0.9.6-gdpstweaks2

Branch: `gdpstweaks2`

## Legacy 1.0 / 1.1 completion-screen ESC workaround

Geometry Dash 1.0 and 1.1 have an old Android-side back-key bug after a level is completed: pressing ESC (Android BACK / keycode 4) can leave the completion screen in a broken state instead of returning to the menu.

For versions whose `GD_GAME_VERSION` begins with `1.0` or `1.1`, the legacy ARM backend now checks whether an `EndLevelLayer` is actually present when ESC is pressed. If it is, the wrapper calls the game's own exported `EndLevelLayer::onMenu()` handler directly. This is the same action as the completion screen's menu button.

Outside an active EndLevelLayer, ESC is delivered normally, so pause/back behavior is unchanged. Versions 1.2 and newer bypass the workaround entirely.
