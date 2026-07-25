# GD Wrapper 0.9.4-arm-performancetest2

Experimental performance branch built from stable bootstrap15.

The normal launcher preserves bootstrap15's memory-integrity, save-transaction, immutable-image, label, allocator, and particle protections. This branch targets the remaining CPU bottleneck with Unicorn translation-cache tuning, faster import dispatch, cached OpenGL entry points, direct guest-memory uploads, and redundant state suppression.

## Build on Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The portable builder downloads Zig, CMake and Ninja into `.build-tools` and writes:

```text
dist-arm-wrapper-performancetest2\
```

No Visual Studio, WSL, MSYS2, administrator rights, or Developer Mode is required.

## Run

Use `RUN_ARM_NATIVE_BOOT.cmd` for the normal integrity-protected benchmark.

Use `RUN_PROFILE_IMPORT_TIME.cmd` for one short diagnostic run. It intentionally adds timing overhead and reports which imports consume actual milliseconds.

Use `RUN_PROFILE_ARM_BLOCKS.cmd` for one short heavy gameplay section. It identifies the hottest guest ARM blocks for targeted native replacement.

Use `RUN_PERFORMANCE_NO_PARTICLE_GUARDS.cmd` only as an A/B comparison after the normal build is verified stable.

See `PERFORMANCETEST1-NOTES.md` for the exact test procedure.
