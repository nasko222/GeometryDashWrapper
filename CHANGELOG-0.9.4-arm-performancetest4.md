# 0.9.4-arm-performancetest4

- Added a live one-second performance/object HUD in the game window title and log.
- Counts `CCNode::visit`, `CCSprite::draw`, sprite-batch draws, particle draws,
  and selected other Cocos draw methods through exact-address Unicorn hooks.
- Reports host OpenGL draw calls, submitted vertices, ARM-to-host imports, FPS,
  average frame time, and per-second maximum node/draw counts.
- Added a low-overhead render HUD that omits all new Cocos hooks.
- Retains PerformanceTest3's massive profiler and stable normal launcher.
