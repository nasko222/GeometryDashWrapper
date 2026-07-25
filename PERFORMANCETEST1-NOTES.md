# PerformanceTest1

This build attacks the steady-state CPU bottleneck identified by bootstrap15 log 38.

Heavy gameplay had no APK reads, no zlib work, no OpenGL client-array copies, and almost no swap cost, yet still executed roughly 2,400-3,000 ARM-to-host imports per frame. PerformanceTest1 changes Unicorn memory/JIT settings and removes avoidable work from the most frequent bridge paths.

## Normal test

Run `RUN_ARM_NATIVE_BOOT.cmd` first. Test the same heavy portions of Clutterfunk and xStep used for bootstrap15, plus repeated deaths and editor load/save. Keep the complete `gd-arm-wrapper.log`.

New output includes:

`ARM Unicorn translation cache ready: TCG cache=256 MiB, CPU TLB=default`

`ARM performance-test profile: gl-proc-cache=... redundant-binds=... direct-uploads=... particle-guards=...`

Compare FPS, render average, render maximum, imports/frame, and the new counters against bootstrap15 log 38.

## Callback timing run

Run `RUN_PROFILE_IMPORT_TIME.cmd` for one short heavy section, then close the game. It adds timing overhead, so its FPS is not comparable with the normal run. Its purpose is the line:

`ARM timed import profile: name:milliseconds,...`

This distinguishes callbacks that are genuinely expensive from callbacks that merely occur often.

## ARM block profiling run

Run `RUN_PROFILE_ARM_BLOCKS.cmd` for one heavy gameplay section. It installs a Unicorn block hook and will run slower than normal. The important output is:

`ARM hot block profile: 0x...(+0x...):callsxbytes,...`

The `+0x...` value is the offset inside `libgame.so`. These addresses identify the game loops worth translating or replacing natively in the next performance build.

## Particle-hook comparison

After confirming the normal run remains corruption-free, run `RUN_PERFORMANCE_NO_PARTICLE_GUARDS.cmd`. This disables only the two PlayLayer particle compatibility code hooks. It may reintroduce the particle-lifetime corruption those hooks protect against, so it is not the default build.

## Rejected experiment

A guest-side Thumb byte loop for tiny `memcpy` operations was tested and deliberately removed. In a local Unicorn microbenchmark, 100,000 sixteen-byte copies were dramatically slower in emulated ARM than the existing direct host-memory helper. PerformanceTest1 does not ship that regression.

## Rejected import-trap gateway

A trap-based import gateway using Thumb `svc`/invalid-instruction callbacks was benchmarked against the existing ranged `UC_HOOK_CODE` gateway. The code hook was about three times faster in the local one-million-call microbenchmark, so PerformanceTest1 keeps the existing hook mechanism instead of shipping a plausible-sounding regression.

## Indexed ZIP accelerator

Claude's analysis of log 38 correctly identified a second, separate problem during level entry: the game's ARM minizip path repeatedly opens and scans the APK. PerformanceTest1 now indexes the APK central directory once and hooks the two exported `CCFileUtils` ZIP methods without replacing their code. The verified ARM ABI is `r1=zip path`, `r2=member name`, and `r3=size output` for `getFileDataFromZip`.

Watch for:

`ARM APK central-directory index ready: entries=...`

`ARM indexed ZIP accelerator ready: get=0x... exist=0x...`

`zip-get/exist/miss/fallback=...`

This mainly targets the 5-6 second level-entry stalls and the millions of tiny `fread` calls. The TCG/import/OpenGL changes target persistent gameplay FPS.
