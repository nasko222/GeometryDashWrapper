# DynarmicTest13 technical notes — debug everything

## Purpose

Test13 is both a performance build and a profiler build. Test12 restored working HTTP upload/download and browser links, but its five-second FPS average could hide isolated 20–100 ms frames. Those isolated frames are especially important on ordinary PCs because missing one refresh deadline can turn a small CPU or GPU stall into a visible gameplay hitch.

## Safe runtime optimizations

### Constant-time guest memory lookup

The emulator previously searched every mapped region for every ARM memory access. A 16-, 32-, or 64-bit read also repeated that search through byte-sized callback calls. Test13 builds a 4 KiB guest-page lookup table and performs typed reads and writes with one lookup, one bounds check, and one `memcpy`. Unaligned and cross-page accesses inside the same mapped region remain supported.

### Cached OpenGL import metadata

Each OpenGL import now caches its resolved host function pointer and argument count. This removes repeated `unordered_map` lookups and string hashing from the render path.

### Buffered diagnostics

Routine JNI, input, file-open, and Android bridge messages are buffered and flushed at periodic profiler checkpoints instead of forcing a disk flush on every event. Errors and fatal diagnostics still flush immediately. This avoids the profiler itself producing storage-related frame stalls on slower PCs.

### Low-overhead host-import sampling

One call out of every 1024 calls to each imported function is timed. The summary estimates which host bridges consume the most time without putting a clock read around every libc or OpenGL trap.

## Debug-everything profiler

The interactive launcher creates:

- `gd-dynarmic-interactive.log` — normal log plus slow-frame records and five-second percentile summaries.
- `gd-dynarmic-profile.csv` — one row per rendered frame.
- `gd-dynarmic-profile-summary.txt` — percentiles, threshold counts, global hot imports, estimated host bridge costs, and the 50 worst frames.

Every frame records:

- complete loop time;
- input/event dispatch time;
- ARM `nativeRender` time;
- `SwapBuffers` time;
- asynchronous OpenGL GPU timer time when supported;
- estimated guest ticks, JIT runs, and SVC traps;
- import, JNI, and OpenGL call counts;
- draw calls and submitted vertex/index counts;
- buffer and texture upload bytes;
- guest allocation/free/reallocation churn and live heap;
- latest Android game-state log message;
- top imports and OpenGL calls for slow frames.

The default slow-frame threshold is 20 ms. It can be changed with:

```text
--slow-frame-ms=25
```

Profiling can be disabled for a clean A/B performance comparison with:

```text
--no-profile
```

## How to test on slower PCs

Use the normal launcher and reproduce the same sequence on each PC: open the game, load Clutterfunk or Xstep, play through a busy section and an ending cutscene, open the editor, then exit normally. Send all three generated files. The summary distinguishes:

- **event/input stalls** — loading or menu action inside a touch callback;
- **ARM CPU stalls** — high `render_ms`, ticks, SVC, or import counts;
- **driver/vsync stalls** — high `swap_ms` with lower render time;
- **GPU stalls** — high `gpu_ms` and draw/vertex counts;
- **allocation stalls** — high allocation/free churn;
- **asset upload stalls** — large texture or buffer upload byte counts.

## Compatibility

All Test12 features remain: working GDPS networking, browser links, host APK member cache, mapped saves, Windows audio, clean exit, editor Space handling, and high-performance GPU preference.
