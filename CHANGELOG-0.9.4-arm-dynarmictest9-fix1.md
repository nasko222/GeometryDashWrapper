# 0.9.4-arm-dynarmictest9-fix1

## Critical startup fix

- Fixed the Test9 `CCFileUtils::getFileDataFromZip` trampoline corrupting guest executable memory during `nativeInit`.
- Test9 used R3 as the absolute-branch scratch register even though R3 carries the `unsigned long*` output-size argument. The host hook consequently wrote member sizes into the import-stub region at `0x2100...`.
- The replacement trampoline now uses R0, preserving R1-R3 exactly for both ZIP hooks.
- Added strict Thumb-bit, function-size, image-range and expected-prologue validation before either hook can be installed.
- Changed the builder revision and output directory so Fix1 cannot accidentally reuse the broken Test9 executable.

## Retained Test9 work

- Host-level APK ZIP index, lazy member decompression, memory cache and persistent member cache.
- Winsock-backed network bridge and cooperative HTTP worker.
- High-performance NVIDIA/AMD GPU preference exports.
- Test8 clean exit and text-safe Space behavior.
