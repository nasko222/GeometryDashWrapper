# GDPSFixes5 notes

Branch: `gdpsfixes5`
Version: `0.9.6-gdpsfixes5`

## User-confirmed result carried forward

Geometry Dash 1.0 color-picker editing is confirmed fixed. The GDPSFixes3
CCFileUtils/extension-resource compatibility work is frozen into this branch.

## Fixes5 corrections

### Extras is now a real in-game UI

GDPSFixes4 used a Win32 child BUTTON and `TrackPopupMenu`. Both paths are gone.
Backends now create cocos2d/GD nodes directly: `ButtonSprite` for controls and
`CCLayerColor` for the popup overlay. Mouse events are host-hit-tested so the
wrapper can trigger injected buttons without requiring a fake guest selector.

### Placeholder level uses ID 10

The previous raw `GJGameLevel::create()` interpretation only produced a blank
level. Static auditing of the early Android `LevelTools::getLevel(int)` shows
IDs 0 through 10 are real jump-table entries; only values above 10 fall back.
The Placeholder action therefore uses `LevelTools::getLevel(10)` and suppresses
background music.

### Time Machine Beta scene budget

The supplied fixes4 log showed `PlayLayer::scene(level 8)` hitting the wrapper's
100,000,000 guest-tick guard after only roughly tens of milliseconds. This was
not a failed level lookup. Extras scene construction/replacement now uses
unlimited guest ticks plus a 15-second wall-clock watchdog.

### Editor shortcut discovery

The supplied fixes4 logs contained no successful editor-command dispatches.
The previous code searched guessed object-field ranges and never found
`EditorUI`. GDPSFixes5 traverses the actual cocos scene tree and child arrays,
then invokes the same move/transform callbacks used by the editor buttons.
Missed shortcuts are explicitly logged instead of silently disappearing.

## Controls

- W/A/S/D: move
- Shift+W/A/S/D: larger move
- Q: rotate counter-clockwise
- E: rotate clockwise

No backend uses `EditorUI::keyDown` for these shortcuts.
