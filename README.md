# Geometry Dash Wrapper 0.9.6-gdpsfixes6

`gdpsfixes6` keeps the Android-only GDPSFixes line. The iOS backend remains removed.

## Confirmed carried fix

The early Android color picker compatibility repair from GDPSFixes3 is retained.
It is now live-confirmed on Geometry Dash 1.0: opening/editing the color picker no
longer crashes the wrapper.

## Launch gate

Normal RUN batches set `I_LOST_THE_GAME=true`. The launcher and all three
backends default it to false and refuse direct execution when it is not true.

## Editor controls

`EDITOR_CONTROLLS=true` enables editor shortcuts on x86, legacy ARM, and ARMv7:

- `W/A/S/D` — normal editor movement.
- `Shift+W/A/S/D` — larger movement commands.
- `Q` — rotate counter-clockwise.
- `E` — rotate clockwise.

GDPSFixes5 replaces the failed raw-pointer search for `EditorUI` with cocos2d
scene/child traversal. It deliberately never routes these keys through the
unsafe Android `EditorUI::keyDown` desktop path.

## In-game Extras menu

`EXTRAS_MENU=true` now renders the Extras interface inside cocos2d. There is no
Win32 BUTTON and no `TrackPopupMenu`. The visible controls are real game
`ButtonSprite` nodes, with an in-game `CCLayerColor` overlay. Host hit testing is
used only to activate the injected controls without inventing a guest callback.

For full Geometry Dash 1.0x through 1.3x:

- **Play Placeholder Level** uses the real early level table slot **ID 10** and
  suppresses background music for that run.

For full Geometry Dash **1.02** only:

- **Play Time Machine Beta** uses built-in level **ID 8**. Scene creation now
  has no artificial 100,000,000 guest-tick cap; a real wall-clock watchdog is
  kept so an actual infinite loop can still be stopped.

Later wrapper backends still receive the in-game Extras button/overlay, with a
`No extras for this version` entry until version-specific extras are added.

## Other carried GDPS fixes

- Large ARM/GDPS level uploads are no longer truncated at 4095 formatted bytes.
- MCI music seeks stop before seeking for Wine/Proton compatibility.
- Legacy ARM networking uses cooperative/nonblocking socket behavior.
- Early color-picker extension resources are mirrored/resolved correctly.
- The GDPSFixes3 ARMv7 build regression using a nonexistent `LocateIndex` method
  remains repaired.

## Building

Run `BUILD_ALL.cmd` on Windows. The source archive contains no APK, extracted
proprietary game library, game executable, or iOS backend.
