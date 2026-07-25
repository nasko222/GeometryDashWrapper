# GD Wrapper 0.9.4-arm-performancetest5

PerformanceTest5 returns to the normal game and adds a dedicated one-second
**gameplay update-path HUD**. It is intended to compare active gameplay with a
paused control sample in the exact same visible level area.

Build on Windows:

```powershell
.\BUILD_WINDOWS.cmd
```

Run the diagnostic launcher:

```text
dist-arm-wrapper-performancetest5\RUN_UPDATE_PATH_HUD.cmd
```

Every second the window title and `gd-arm-wrapper.log` report:

- ACTIVE or PAUSED/STATIC and the percentage of frames that ran PlayLayer update
- FPS and average full frame time
- PlayLayer update, collision, visibility, spawn and camera calls per frame
- Player update, jump and actual object-collision callbacks per frame
- GameObject activation and deactivation calls per frame
- Cocos scheduler and action-manager updates per frame
- OpenGL draw calls, submitted vertices and ARM-to-host imports per frame

Play into the laggy section for several seconds, pause without leaving that
section for several seconds, then resume. The paused sample keeps the visible
scene while suppressing most gameplay simulation, making the comparison useful.

The normal launcher is unchanged. PerformanceTest4 object/render HUD launchers
and the PerformanceTest3 massive profiler remain available.
