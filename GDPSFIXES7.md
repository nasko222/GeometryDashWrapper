# Geometry Dash Wrapper 0.9.6-gdpsfixes7

This is a regression-fix build based on gdpsfixes6.

## 1.3 GDPS v7-packaged legacy startup

The legacy ARM Dynarmic environment now implements the exclusive-write callbacks
used by ARMv7 `STREX`. gdpsfixes6 installed an `ExclusiveMonitor` but omitted
these callbacks, causing `libascella.so` to loop forever in an
`LDREX`/`STREX` atomic increment during its first constructor.

## Extras temporarily removed

Extras is hard-disabled even if an old launcher or batch exports
`EXTRAS_MENU=true`. ARMv7 also rolls back the fixes5 in-game Extras scene-tree
scanner and injected overlay/button path.

## 2.2 beta platformer controls

Platformer playtest input now takes priority over editor hotkeys. Mouse
platformer handling uses the live `LevelEditorLayer` during editor playtest
instead of relying on GameManager's stale PlayLayer pointer, with a jump fallback
if the beta does not expose a usable UILayer there.

## 2.2 beta editor save/exit regression

ARMv7 scene/editor discovery is restored to the pre-Extras gdpsfixes4 behavior.
This removes the fixes5 scene-tree traversal/UI injection from editor teardown,
the main behavioral change present in the crashing save-and-exit path.
