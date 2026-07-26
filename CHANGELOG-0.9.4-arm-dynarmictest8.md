# 0.9.4-arm-dynarmictest8

## Loading acceleration

- Read-only opens of `game.apk` now use the APK bytes already resident in host memory.
- `fread`, `fseek`, and `ftell` support memory-backed guest file handles.
- Guest `fread`/`fwrite` now transfer directly to mapped guest memory instead of allocating a temporary host vector for each call.
- Added early dispatch paths for the libc calls that dominate level ZIP parsing (`fread`, string compares, memory copies, mutex no-ops, and related operations).
- Reduced diagnostic import-history overhead by sampling normal libc traps while retaining fatal calls.
- Added end-of-run APK-memory read totals to the log.

## Input and shutdown

- `terminateProcess()` now requests clean host shutdown, closes audio, exits the render loop, and destroys the Win32 window.
- Android IME open/close and keyboard-state calls now track whether a text field is active.
- Space no longer emits a gameplay touch while text input is active; it remains available as a typed character.

## Retained from Test7

- mapped Bionic-compatible guest `FILE` objects and persistent save stability
- Windows background music and effects
- reclaiming guest allocator
- symbolized fatal diagnostics and heap reports
