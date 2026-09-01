# Geometry Dash Wrapper 0.9.7-cof4

`cof4` means **Cleanup Optimization Fixes 4**. It builds directly on COF3 and
keeps the cleaned Endurance/companion handling plus the COF3 FPS selector.

## COF4 correction — Geometry Dash 1.02 comments

Only the **Geometry Dash / Geometry Dash Lite 1.02 release generation** gets
the comments shortcut. Detection uses the manifest build generation plus the
native comment capability rather than a literal `"1.02"` version-name check.

On the normal level screen, press **C**. COF4 now uses the actual hidden native
comments sequence:

```text
LevelInfoLayer + 0x150 -> GJGameLevel*
InfoLayer::create(level)
InfoLayer::show()
InfoLayer::loadPage(0)
```

`loadPage(0)` is the native 1.02 path that builds the `GJCommentListLayer` and
requests page 0 through `GameLevelManager::getLevelComments`. COF3 incorrectly
called `LevelInfoLayer::onInfo()`, which is only the brown level-description
popup.

1.0 and 1.1+ do not get this hotkey. No dislike control is added.

## FPS / VSync

Both launch scripts default to:

```bat
set "FPS=VSYNC"
```

You can replace it with a numeric cap, for example `144`, `240`, or `9999`.

- `FPS=VSYNC`: swap interval 1, no numeric host cap.
- numeric FPS: swap interval 0, high-resolution host cap at that FPS.
- valid numeric range: 1-10000.
- invalid/missing values fall back to VSYNC.

The setting applies to x86, ARM-legacy and ARMv7.

## ARMv7 2.2 policy

The COF cleanup remains unchanged: the wrapper does **not** fabricate the
stock 2019/2022/2023 2.2 editor runtime. The modded 2023 beta uses its
validated APK-provided companion editor path with the Endurance-era fixes.

## Build

On 64-bit Windows run `BUILD_ALL.cmd`. Output is written to `dist-unified\`.
Drag an APK onto `RUN_AUTO_GDPS.cmd` or `RUN_AUTO_BOOMLINGS.cmd`.

See `COF1.md`, `COF2.md`, `COF3.md`, and `COF4.md` for branch details.
