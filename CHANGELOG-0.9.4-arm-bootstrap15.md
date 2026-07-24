# Geometry Dash ARM Wrapper 0.9.4-arm-bootstrap15

Bootstrap15 is an experimental performance release based on bootstrap14's
stable corruption/save behavior.

- Mapped writable guest RAM onto host-owned backing memory with
  `uc_mem_map_ptr`, allowing safe bridge-side reads/writes without a full
  Unicorn memory API round trip.
- Added direct paths for memcpy/memmove/memset/memcmp, C strings, zlib streams,
  CRC/uncompress, guest file reads, and OpenGL client vertex/index arrays.
- Replaced repeated string-based hot-import dispatch with import classification
  and batched ARM register reads/writes.
- Executed single-threaded pthread mutex/condition no-ops in tiny guest ARM
  stubs, avoiding their ARM-to-host callbacks entirely.
- Cached decompressed APK members with a bounded 512-entry / 256 MiB cache while
  retaining the existing immutable full-APK cache.
- Disabled deep DS_Dictionary and level-string code hooks by default; opt back
  in with `--deep-diagnostics` for corruption investigations.
- Throttled repetitive constructor and zlib stream diagnostics so logging does
  not add avoidable startup/level-load stalls on low-end systems.
- Kept decoded MCI effect voices open after preload and recycled them on play,
  targeting the confirmed wrapper-side death/effect delay.
- Expanded five-second profiling with direct-memory, direct-array, and
  APK-member-cache counters.
- Added `BUILD_WINDOWS.cmd` / `build-windows.ps1`, which download verified
  portable tools and build everything on ordinary 64-bit Windows without WSL,
  Linux, admin rights, or system-wide installation.
