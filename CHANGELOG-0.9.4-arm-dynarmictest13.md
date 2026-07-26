# 0.9.4-arm-dynarmictest13

- Added the “debug everything” frame profiler with per-frame CSV output and a human-readable summary.
- Added p50/p90/p95/p99/p99.9/max frame-time reporting and counts above 16.667, 20, 25, 33.333, and 50 ms.
- Added separate event, ARM render, buffer-swap, and asynchronous GPU timings.
- Added per-frame imports, JNI calls, OpenGL calls, draw calls, submitted vertices, upload bytes, guest ticks, JIT runs, SVC traps, and heap churn.
- Added detailed slow-frame dumps with the latest game-state message and top import/OpenGL deltas.
- Added final global import and OpenGL rankings plus sampled host-bridge cost estimates.
- Replaced linear guest-memory region searches and recursive byte reads/writes with a 4 KiB page lookup and single-copy typed accesses.
- Cached resolved OpenGL functions and argument descriptors per import.
- Buffered routine diagnostics to avoid forced disk flushes during gameplay.
- Added host CPU/RAM/logical-core information and process CPU/memory interval statistics.
- Preserved Test12 networking, browser links, audio, saves, APK member caching, clean exit, text input, and GPU preference.
