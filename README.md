# Geometry Dash Wrapper 0.9.6-gdpsfixes7

`gdpsfixes7` keeps the Android-only GDPSFixes line. The iOS backend remains removed.

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

Editor shortcuts remain available while editing, but gameplay/playtest input now
takes priority so platformer WASD/Space is not consumed by editor controls.

## Extras

Extras is temporarily removed/disabled in this build. Old `EXTRAS_MENU=true`
environment settings are ignored, and no Extras button or overlay is created.

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
