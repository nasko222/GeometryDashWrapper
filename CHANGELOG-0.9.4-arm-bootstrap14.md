# Geometry Dash ARM Wrapper 0.9.4-arm-bootstrap14

- Kept bootstrap13's stable 512 MiB guest allocator and level-data behavior.
- Added a shared, immutable in-memory APK image with independent guest file
  positions, eliminating repeated Windows open/read cycles during Cocos ZIP
  lookups and suppressing hundreds of synchronous duplicate log flushes.
- Reused bounded host I/O scratch memory and persistent OpenGL client-array and
  index buffers instead of allocating/freeing temporary buffers on every read,
  write, and draw.
- Added low-overhead five-second profiles for ARM render and SwapBuffers time,
  slow-frame counts, draw/vertex/client-copy load, guest allocation traffic,
  APK/file reads, zlib calls, and the six hottest guest imports.
- Added individual timing diagnostics for slow native input/render calls so a
  level-load stall can be separated from in-level rendering cost.
- Expanded non-destructive save recovery to ignore empty, XML-header-only, and
  truncated XML plist game files in addition to the known 22-byte failure file.
- Reused the shared APK image for first-time MP3/Ogg extraction; audio playback
  semantics remain unchanged pending phone comparison.
