# Unified7 Fix1 — Runtime Log Cleanup

Before every `RUN_AUTO.cmd` launch, `run_auto.py` now deletes known x86, ARM legacy, ARMv7, profiling, import-manifest, and earlier NetworkTest runtime outputs from `dist-unified`.

If a previous wrapper process still owns a file and deletion fails, startup aborts instead of silently leaving a stale log behind.

Each successful launch also creates `gd-run-info.txt` containing the UTC start time, selected backend, game identity, package name, APK path, and APK byte size. Include this marker when sending a test ZIP.

The direct generated backend `RUN.cmd` and `RUN_DEBUG.cmd` scripts receive equivalent cleanup commands during builds.
