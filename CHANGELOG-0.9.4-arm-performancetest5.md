# 0.9.4-arm-performancetest5

- Added a one-second active-gameplay update-path HUD.
- Counts PlayLayer update, collision, visibility, spawn and camera routines.
- Counts player update/jump and actual object-collision callbacks.
- Counts object activation/deactivation and scheduler/action-manager updates.
- Labels each interval ACTIVE or PAUSED/STATIC using PlayLayer update activity.
- Keeps FPS, frame time, GL draws, vertices and ARM-to-host imports in the same
  line for direct correlation.
- Added `RUN_UPDATE_PATH_HUD.cmd`.
- Preserved the normal stable launcher and previous profiler launchers.
