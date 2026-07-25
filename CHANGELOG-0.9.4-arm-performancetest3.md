# 0.9.4-arm-performancetest3

- Drops aggressive pretranslation from the normal launcher after PerformanceTest2 showed no steady-state benefit.
- Adds an F11-controlled 10-second massive ARM block profiler.
- Maps the 32 hottest guest blocks to nearest ELF symbols.
- Measures import callback body time and estimates remaining guest/TCG execution time.
- Reports p50/p90/p95/p99 frame-time percentiles.
- Expands the block table to 262,144 entries.
- Keeps all bootstrap15 integrity and save protections.
