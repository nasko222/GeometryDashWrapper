# OverkillTest3 notes

The OverkillTest2 log showed that removing audio, replacing PNGs with 1x1 textures, removing ARM draw functions, and removing CCNode scene traversal did not explain the remaining gameplay cost. The next missing measurement is the exact translated ARM code active during sustained lag.

Run `RUN_AUTO_PROFILE_ORIGINAL.cmd`, reach the genuinely slow Clutterfunk/Xstep section, and keep playing. No hotkey is required. After ten consecutive clean render calls at or above 22 ms, the wrapper automatically enables the ARM block profiler for five seconds and logs `ARM hot block profile (completed 5-second sample): ...`. Loading stalls are excluded when the render performed APK, file, or zlib work.

The block profiler intentionally makes its five-second sample slower. This is diagnostic. Up to three samples are captured with ten-second cooldowns.
