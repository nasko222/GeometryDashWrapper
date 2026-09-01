# Geometry Dash Wrapper 0.9.7-cof3

`cof3` means **Cleanup Optimization Fixes 3**. It builds directly on COF2 and
keeps the cleaned-up Endurance/companion handling for the modded 2023 beta.

## COF3 additions

### Geometry Dash 1.02 comments

Only the **Geometry Dash / Geometry Dash Lite 1.02 release generation** gets the new comments shortcut. Detection uses the
manifest build generation plus the native comment capability rather than a
literal `"1.02"` version-name comparison. On its normal level information
screen, press **C** to call the game's own `LevelInfoLayer::onInfo()` and open
the original native comments browser.

The accepted package families are `com.robtopx.geometryjump` and
`com.robtopx.geometryjumplite`; the 1.02 release generation is manifest
versionCode 4 and must also expose the native comment functions. 1.0 and 1.1+
do not get the hotkey. No dislike control is added.

### FPS / VSync

Both launch scripts now default to:

```bat
set "FPS=VSYNC"
```

You can replace it with a numeric cap, for example:

```bat
set "FPS=144"
set "FPS=240"
set "FPS=9999"
```

- `FPS=VSYNC`: swap interval 1, no numeric host cap.
- numeric FPS: swap interval 0, high-resolution host cap at that FPS.
- valid numeric range: 1-10000.
- invalid/missing values fall back to VSYNC.

The setting applies consistently to x86, ARM-legacy and ARMv7.

## ARMv7 2.2 policy

The COF1/COF2 cleanup remains unchanged: the wrapper does **not** fabricate the
stock 2019/2022/2023 2.2 editor runtime. The modded/selected 2023 beta uses its
validated APK-provided `LevelEditorLayerExt::initH` companion path, with the
Endurance-era gameplay/editor fixes retained.

## Build

On 64-bit Windows run:

```bat
BUILD_ALL.cmd
```

Output is written to `dist-unified\`. Drag an APK onto `RUN_AUTO_GDPS.cmd` or
`RUN_AUTO_BOOMLINGS.cmd`.

See `COF1.md`, `COF2.md`, and `COF3.md` for branch details.
