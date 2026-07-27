# NetworkTest5 notes

- Runtime baseline: NetworkTest4.
- Runtime files changed: only `src/dynarmic_probe.cpp`.
- Worker: synchronous signal-to-wait execution; no render-frame slices.
- DNS: synchronous in worker.
- Connect/poll: restored from DynarmicTest14.
- Diagnostics: NetworkTest4 import ring and network/thread tracing retained.
- APKs: excluded from source release.
