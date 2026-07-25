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

## Windows builder hotfix 1

- Normalize CMake compiler, archiver and ranlib wrapper paths to forward slashes.
- Automatically discard the invalid CMake cache produced by the original builder.
- Fixes `Invalid character escape '\G'` on drives such as `D:\GDOldVersionsProject`.

## Windows builder hotfix 2

- Invoke `zig.exe cc -target x86-windows-gnu` directly through CMake's supported
  compiler-command list instead of reconstructing compiler arguments in a batch
  file.
- Fixes Zig receiving the invalid bare option `-std` when Ninja supplied
  `-std=gnu11`.
- Automatically refreshes the Unicorn CMake tree from older builder revisions.

## Windows builder hotfix 3

- Preserve `zig cc -target x86-windows-gnu` when Unicorn invokes QEMU's legacy
  shell configure stage, instead of passing bare `zig.exe`.
- Generate and verify `arm-softmmu/config-target.h` before Ninja compilation.
- Treat QEMU configure/header-generation failures as fatal CMake errors rather
  than continuing into misleading missing-header compiler failures.
- Automatically refreshes the Unicorn CMake tree from builder3 and older.

## Windows builder hotfix 4

- Remove the QEMU POSIX-shell configure stage from the ordinary Windows build.
- Use dedicated pre-generated Win32/Zig host and ARM target configuration
  headers bundled with the patched Unicorn source.
- Skip the invalid bare `zig.exe -dumpmachine` probe; the builder already fixes
  the host target explicitly as 32-bit `x86-windows-gnu`.
- Avoid inheriting Unicorn's MSVC-only 128-bit atomic setting on the 32-bit Zig
  target.
- Automatically refreshes the Unicorn CMake tree from builder4 and older.

## Windows builder hotfix 5

- Skip Unicorn's legacy `unicorn.o` symbolic-link alias on Windows; the wrapper
  already links directly against `libunicorn.a`.
- Fixes `A required privilege is not held by the client` on the final 70/70
  archive step without requiring Developer Mode or administrator rights.
- Preserve builder5's already compiled Unicorn object cache, so an upgraded
  failed build normally reruns only the final archive rule.
