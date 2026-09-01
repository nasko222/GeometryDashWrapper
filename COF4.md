# Geometry Dash Wrapper 0.9.7-cof4

COF4 is a focused correction over `0.9.7-cof3`.

## Geometry Dash 1.02 comments hotkey correction

COF3 correctly detected the 1.02 release generation and intercepted **C**, but
it called `LevelInfoLayer::onInfo()`. Runtime testing proved that callback opens
the brown level-description popup, not the dormant comments list.

The shipped 1.02 ARM library shows the real native sequence:

- `LevelInfoLayer + 0x150` is the current `GJGameLevel*`.
- `InfoLayer::create(GJGameLevel*)` constructs the hidden information/comments layer.
- `InfoLayer::show()` presents it.
- `InfoLayer::loadPage(0)` switches it to the dormant comments path.
- `loadPage(0)` calls `setupCommentsBrowser(nullptr)` and then
  `GameLevelManager::getLevelComments(levelID, 0)`.

COF4 therefore makes **C** execute exactly that sequence. It no longer invokes
`LevelInfoLayer::onInfo()`.

The build gate is unchanged from corrected COF3: this is only enabled for the
Geometry Dash / Geometry Dash Lite 1.02 release generation when the native
comment ABI is present. There is no dislike UI.

## FPS

No FPS/VSync behavior changed from COF3. `FPS=VSYNC` remains the default;
numeric values disable VSync and use the shared high-resolution host cap.

## Other backends

COF2/COF3 x86, ARMv7 and ARM-legacy fixes are otherwise retained. The cleaned
2.2 policy remains unchanged.
