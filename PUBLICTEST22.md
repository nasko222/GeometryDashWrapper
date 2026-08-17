# PublicTest22

PublicTest22 is a level-loading stability build for the iOS ARMv7 backend.

The PublicTest21 Windows logs reached real bundled Forlorn level loading. Two independent failures were exposed:

- Forlorn 0.101 exhausted the wrapper guest heap while loading level assets. The wrapper used a bump allocator: `free` and old `realloc` blocks were only removed from bookkeeping and were never reusable. PublicTest22 adds a coalescing free list, reusable freed blocks, and in-place shrinking for `realloc`.
- Forlorn 1.9c progressed farther but faulted at low address `0x14c`. PublicTest22 adds complete ARM register, import-slot, and guest-heap diagnostics to every memory fault so the next Windows log identifies the exact context instead of ending at the generic fault line.

The 128 MiB guest heap mapping is intentionally unchanged. This build fixes reuse rather than hiding the allocator leak by enlarging the address range.

All PublicTest21 URL/plist fallback, NPOT PVR, UIKit text, input, Foundation, and scene-flow work is retained.
