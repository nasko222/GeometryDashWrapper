# Geometry Dash Wrapper 0.9.6-gdpsfixes1

`gdpsfixes1` is an Android-only maintenance branch focused on GDPS reliability and
legacy Geometry Dash compatibility. Non-Android experimental platform code and its launcher/build paths are not part of this branch.

## Included backends

- x86 native Android wrapper for supported x86 Geometry Dash builds.
- Dynarmic legacy ARM backend for Geometry Dash 1.0-1.4-era ARM APKs.
- Dynarmic ARMv7 backend for later ARMv7 builds / the existing 2.2-beta path.

## GDPSFixes1 changes

### Large level uploads

The ARM printf/sprintf compatibility bridge no longer truncates a formatted `%s`
at 4095 bytes. `snprintf`'s required size is now honored and long values are
reformatted into a dynamically sized buffer (bounded by the wrapper's existing
16 MiB guest-format limit). This applies to both Dynarmic ARM backends.

### Background music seeking

The shared Windows MCI music backend now stops the MPEG alias before seeking.
This targets Wine/Proton failures where retry/death music continued from the old
position and StartPos seeks failed with MCI error 277. Because the audio bridge
is shared, the behavior applies across wrapper backends.

### Slow/unreachable GDPS connections

The legacy ARM socket bridge now honors guest nonblocking mode. A pending
nonblocking `connect()` returns Android/POSIX `EINPROGRESS` instead of blocking
the render/input callback for up to 15 seconds; nonblocking `recv()` returns
`EAGAIN` immediately. The ARMv7 path already used this model.

## Still open

- Geometry Dash 1.0.0's original background-color/BG-trigger crash needs a
  binary-level comparison against Android 1.0.1 before a safe compatibility
  patch is installed.
- Editor WASD/Q shortcuts need a verified editor callback ABI for each supported
  game family. The known unsafe `EditorUI::keyDown` route is deliberately not
  used.
- Cursor hiding and pause-button removal are deliberately not included.

## Building

Run `BUILD_ALL.cmd` on Windows. No APK or proprietary game executable is bundled.
Drag a supported Android APK onto `RUN_AUTO_BOOMLINGS.cmd` or `RUN_AUTO_GDPS.cmd`
after building.
