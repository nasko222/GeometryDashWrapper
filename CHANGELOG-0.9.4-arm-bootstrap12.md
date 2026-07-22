# Geometry Dash ARM Wrapper 0.9.4-arm-bootstrap12

- Removed the 64 KiB ceiling from imported ARM `strlen`, string copying,
  comparison, search, formatting, and duplication paths.
- Replaced repeated whole-tail `strtok` copies with a linear chunk scanner,
  avoiding truncation and quadratic work while parsing large level strings.
- Added C++ level-string diagnostics at `PlayLayer`, `LevelEditorLayer`,
  `GJGameLevel`, and `GameLevelManager` load/save boundaries.
- Added real guest callback execution for `pthread_once` and deterministic
  single-guest-thread `pthread_getspecific`/`pthread_setspecific` storage.
- Added a host-side immutable-image write guard. It blocks bridge writes that
  bypass Unicorn's guest memory-write hook and logs the exact bridge and line.
- Reordered effect resolution so decoded WAVs are remembered in-process and
  compatible checksum-named cache files are reused before reopening the APK.
- Removed the one-byte guest allocation leak in imported `getc`/`fgetc`.
- Retained bootstrap11's authentic comparator-backed `qsort`, save byte-count
  diagnostics, label checks, and the paired particle ownership compatibility
  path.
