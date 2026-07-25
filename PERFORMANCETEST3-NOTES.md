# PerformanceTest3: Massive Profiler

PerformanceTest2 proved that aggressive pretranslation is not the steady-state fix: 136,601 requested blocks were cached and retained before startup, but gameplay performance was unchanged. PerformanceTest3 returns to normal on-demand translation and adds a targeted profiler.

## Test

1. Run `RUN_ARM_NATIVE_BOOT.cmd` to confirm normal behavior.
2. Run `RUN_MASSIVE_PROFILER.cmd`.
3. Reach the exact heavy Clutterfunk/Xstep/Cycles section.
4. Press **F11 once**. The wrapper profiles ARM translation-block execution for 10 seconds, then automatically disables the block hook and restores normal-speed translations.
5. Close the game and send `gd-arm-wrapper.log`.

The report includes frame percentiles, total render time split between import callback bodies and the guest/TCG remainder, the 16 most expensive imports by measured wall time, and the 32 hottest ARM blocks mapped to the nearest ELF symbol. Profiling intentionally makes those 10 seconds much slower.

Generated x86 translation blocks cannot be treated as a portable executable file: they contain process-specific pointers, CPU-state assumptions, helper addresses and direct block links. Persisting them would improve startup only. PerformanceTest2 already retained the equivalent in memory and showed no gameplay gain.
