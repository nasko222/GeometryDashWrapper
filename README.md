# Geometry Dash Wrapper 0.9.6-gdpstweaks2

`gdpstweaks2` continues the Android-only wrapper line with desktop usability tweaks and removes the abandoned 1.3 hybrid experiment.

## Confirmed carried fix

The early Android color picker compatibility repair from GDPSFixes3 is retained.
It is now live-confirmed on Geometry Dash 1.0: opening/editing the color picker no
longer crashes the wrapper.

## Launch gate

Normal RUN batches set `I_LOST_THE_GAME=true`. The launcher and all three
backends default it to false and refuse direct execution when it is not true.

## Editor controls

`EDITOR_CONTROLLS=true` enables editor shortcuts on x86, legacy ARM, and ARMv7:

- `W/A/S/D` — big editor movement step.
- `Shift+W/A/S/D` — small editor movement step.
- `Q` — rotate counter-clockwise.
- `E` — rotate clockwise.

Editor shortcuts remain available while editing, but gameplay/playtest input now
takes priority so platformer WASD/Space is not consumed by editor controls.


## Window controls

- Drag the normal Windows frame to resize; rendering keeps the game aspect ratio with letterboxing.
- `F11` or `Alt+Enter` toggles borderless fullscreen on the current monitor.
- Mouse/touch mapping follows the displayed game area, including after resize/fullscreen.

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
