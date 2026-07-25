# DynarmicTest6 notes

This build fixes the level-start `std::bad_alloc` captured by DynarmicTest5.

## Root cause

- The Test5 heap was a 128 MiB bump allocator.
- Imported `free()` and `munmap()` did not reclaim memory.
- Imported `realloc()` allocated a new block but did not release the old one.
- The Dynarmic qsort bridge allocated a new guest scratch block inside its inner comparison loop and never released it.
- The fatal log placed the exception object at `0x37ffed28`, immediately below the old heap end `0x38000000`.

## Test6 changes

- Real guest free-list allocator with splitting, coalescing, best-fit reuse and heap-top trimming.
- Correct free/calloc/realloc/mmap/munmap reclamation behavior.
- In-place realloc shrinking and growth where possible.
- qsort now allocates one scratch block per call and releases it.
- Heap arena headroom increased from 128 MiB to 256 MiB.
- Allocation failures and fatal diagnostics include arena/live/free/peak statistics.
- Existing Test5 crash diagnostics remain enabled.
