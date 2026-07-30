# Building EnduranceTest9

Use `BUILD_ALL.cmd` from a Windows command prompt. The scripts download/use the
same pinned CMake, Ninja, Zig and Dynarmic dependencies as EnduranceTest8.

The archive intentionally contains no APK, extracted Android library, built EXE
or DLL, Python dependency, `.git` directory or `.gitignore`. Place or drag an APK
only after building, using `RUN_AUTO_GDPS.cmd` or `RUN_AUTO_BOOMLINGS.cmd`.

The runtime changes are limited to the ARMv7 editor background/batch-node bridge
and removal of the failed legacy audio experiment. The x86 backend source is
unchanged from EnduranceTest8.
