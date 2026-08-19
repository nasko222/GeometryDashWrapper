# 0.9.6-gdpsfixes1

Branch: `gdpsfixes1`

## Reported issues assimilated

1. GD 1.0 music can continue from the death position instead of restarting.
2. StartPos can produce no music at the requested timestamp; Restart can freeze
   when a StartPos exists.
3. Search/online actions can appear frozen for many seconds on a bad connection.
4. Original GD 1.0.0 can crash in the background-color/BG-trigger color menu;
   Android 1.0.1 reportedly fixes the game bug.
5. Critical: ARM level upload data can be cut to exactly 4095 bytes, corrupting
   uploaded GDPS levels.
6. Requested editor keyboard controls: WASD + Q. Cursor hiding and pause-button
   removal are explicitly out of scope because prior wrapper experiments caused
   regressions.

## Implemented in this branch

- Removed the unrelated experimental platform backend and kept this branch Android-only.
- Fixed the exact 4095-byte formatted-string truncation in legacy ARM and ARMv7.
- Made MCI music seek operations stop-before-seek for Wine/Proton compatibility.
- Made legacy ARM nonblocking connect/recv obey EINPROGRESS/EAGAIN instead of
  synchronously waiting on the UI/render callback.

## Pending binary-specific work

The 1.0.0 color crash is not patched without a verified Android 1.0.0/1.0.1
binary diff. Editor WASD/Q is also intentionally not routed through the known
unsafe `EditorUI::keyDown` ABI; it needs a verified direct editor transform
callback for each supported game family.
