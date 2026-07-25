# 0.9.4-arm-dynarmictest3-fix1

- Removed the artificial 500,000,000 guest-tick cutoff from `nativeInit`.
- Added a 120-second wall-clock guard for `nativeInit`.
- Added a 30-second wall-clock guard for each first-frame `nativeRender` call.
- Added five-second guest execution progress snapshots.
- Added exact PC/LR/SP/CPSR diagnostics on timeout or unexplained stops.
- Added nearest dynamic symbol and ELF-offset resolution.
- Added recent import/JNI/JavaVM trap history and top-import counts.
- Added distinct `gd-dynarmic-first-frame.log` and `gd-dynarmic-probe-only.log` outputs.
- Preserved the existing Dynarmic/Zig/CMake/Ninja/Boost build cache.
