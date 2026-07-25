# PerformanceTest4 live object correlation test

The normal launcher is unchanged. Use `RUN_LIVE_OBJECT_HUD.cmd` only for the
diagnostic comparison. The window title and log update once per second.

Metrics:

- `nodes/frame`: calls to `CCNode::visit`; includes visible nodes and containers.
- `cocos-draw/frame`: measured sprite, sprite-batch, particle, atlas/layer/progress
  draw-method entries. It is not a perfect unique-object count.
- `gl-draws` and `vertices`: actual host OpenGL submissions seen by the bridge.
- `imports`: ARM-to-host transitions for that second.
- `nodes-max` and `draw-max`: busiest individual frame during the second.

Exact Cocos hooks have overhead. Compare the same location with
`RUN_RENDER_HUD_LOW_OVERHEAD.cmd`; in that mode `nodes`/Cocos draw values remain
zero while the existing bridge counters continue updating.

The most useful evidence is whether FPS follows nodes, Cocos draw entries,
vertices, imports, or only one of them as the screen becomes denser.
