# 0.9.4-arm-dynarmictest6

- Fixed deterministic `std::bad_alloc` while opening Stereo Madness or another level.
- Replaced the no-free bump allocator with a reclaiming guest heap.
- Fixed a qsort scratch allocation leak.
- Reclaims old storage on realloc and mmap/munmap.
- Increased the mapped guest heap from 128 MiB to 256 MiB as additional level-loading headroom.
- Retains Test5 symbolized fatal diagnostics and adds heap statistics.
