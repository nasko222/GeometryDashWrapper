# Bootstrap15 performance diagnosis

## What the new logs show

Bootstrap14's full APK cache is working. Each run loads `game.apk` once into a
shared 23.56 MiB buffer, and guest `fopen` calls over the APK use independent
seek positions on that same memory image. Re-reading the APK from disk is not
the steady-gameplay bottleneck.

The low-end traces separate two costs:

1. **Level/startup work.** There is real zlib, file parsing, allocation, and
   long string/dictionary work while resources and level strings are decoded.
   On the first rendered frame, the guest also crosses the ARM-to-host import
   bridge roughly two million times, dominated by `strcmp`, `strlen`, mutex
   calls, allocation, and zlib.
2. **Steady rendering.** The Intel traces remain around 9–21 FPS in dense
   scenes even when `apk-opens=0`, `file-opens=0`, and `zlib=0`. Those frames
   still perform thousands of bridge calls and hundreds of OpenGL client-array
   copies per frame. This proves that caching LevelData alone cannot make the
   renderer smooth.

The friend log reports **Intel UHD Graphics**, not a GTX 1050. The work-PC log
reports **Intel HD Graphics 2500**. The RTX 4060 trace has enough CPU/GPU margin
to hide much of the wrapper overhead, while the Intel systems do not.

## Why bootstrap15 is not a full ARM-to-x86 ahead-of-time translator

A whole-library ARM-to-x86 translator would be a new recompiler backend, not a
small cache change. It would need correct handling for ARM/Thumb mode changes,
relocations, indirect branches, callbacks, JNI/import exits, floating point,
self-referential data, exceptions, code invalidation, and synchronization with
guest memory. That is possible as a longer project, but attempting it in one
bootstrap would put the now-stable save/level behavior at high risk.

Bootstrap15 instead attacks the measured boundary overhead while preserving
the existing execution model:

- writable guest RAM is host-backed with `uc_mem_map_ptr`;
- hot memory/string/zlib/file bridge operations access mapped memory directly;
- OpenGL client vertex/index arrays point directly at stable mapped RAM during
  each draw instead of being copied out of Unicorn memory;
- six mutex/condition imports that already behaved as single-threaded no-ops
  execute as tiny guest ARM stubs and no longer exit to the host;
- import dispatch is classified once and ARM registers are read/written in
  batches;
- immutable decompressed APK members are cached with a 256 MiB bound;
- expensive parser tracing is disabled unless `--deep-diagnostics` is used;
- repetitive zlib/constructor diagnostics are throttled;
- MCI effect decoders remain open and are recycled, removing the fresh decoder
  open from the normal death/effect playback path.

## LevelData caching decision

The wrapper does **not** blindly cache decoded editable level strings. The
editor mutates those strings and the game saves them, so a stale cache could
reintroduce exactly the level/save corruption that bootstrap14 fixed. Instead,
bootstrap15 caches immutable APK members and accelerates the zlib buffers and
string/memory bridge used by level parsing. A decoded-level cache can be added
later only with explicit invalidation on edit/save and a reliable level identity
key.

## Validation status

The modified C sources pass strict syntax checks against the bundled Unicorn
and zlib headers, and all Python build scripts compile. The Windows builder and
hashes were reviewed, including the correct 64-bit CMake archive hash. This
Linux runtime did not contain a Windows cross-linker and blocked fetching one,
so no bootstrap15 EXE is claimed as tested here. Build and runtime validation
must be done on Windows with `BUILD_WINDOWS.cmd`.

## Recommended test sequence

1. Start with an empty copied `save` folder and confirm menu boot.
2. Play Clutterfunk, Xstep, and Cycles for at least 30 seconds each.
3. Enter the editor, create/edit/save a level, restart, and verify it remains
   intact.
4. Die repeatedly and compare sound onset against the visual death.
5. Re-enter the same level twice and compare both load times.
6. Send the complete `gd-arm-wrapper.log`, especially the new direct-memory,
   direct-array, cache-hit, import, zlib, and frame profile counters.
