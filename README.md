# GD Wrapper 0.9.4-arm-performancetest4

PerformanceTest4 returns to the stable ARM wrapper and adds a live one-second
correlation HUD.

Build on Windows with:

```powershell
.\BUILD_WINDOWS.cmd
```

Run the full object counter:

```text
dist-arm-wrapper-performancetest4\RUN_LIVE_OBJECT_HUD.cmd
```

The game window title and `gd-arm-wrapper.log` update every second with FPS,
frame time, Cocos nodes visited, sprite/batch/particle/other draw methods, host
OpenGL draws, submitted vertices, and ARM-to-host import transitions. The full
object hooks add overhead, so repeat the same section with
`RUN_RENDER_HUD_LOW_OVERHEAD.cmd` for a bridge-only baseline.

The normal stable launcher remains `RUN_ARM_NATIVE_BOOT.cmd`.
