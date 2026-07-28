# Unified7 Fix4 native stabilization

## Evidence from the supplied dated logs

- Every ARMv7/2.2 run stopped before initialization with
  `--companion-hooks must be off, safe, or all`. This was a launcher/backend
  argument boundary failure, not a new game crash.
- No x86 `gd-wrapper.log` was captured. The x86 backend changed its current
  directory to `x86\` before opening a fixed relative log name, while the
  launcher only harvested files from the distribution root.
- Geometry Dash 1.0 reached all constructors and JNI initialization, then failed
  inside `nativeInit` at the CCSet vtable. Geometry Dash 1.01 completed normally.
- 1.8, 1.90 and 1.93 each had failed starts followed by successful starts in the
  same test sequence, so they are not classified as universally unsupported.

## Changes

- Native launcher; no Python at runtime.
- PowerShell/Zig x86 builder; no Python at build time.
- Dedicated GDPS and Boomlings BAT launchers.
- Direct absolute per-run x86 log path.
- ARMv7 companion hooks OFF by default and no launcher option required.
- Fix3 editor/shader and keyboard compatibility changes retained without the
  Fix2 network interception experiment.
- Exact 1.0 host-minizip and forced-HD guards retained.
- No per-version save directories and no cloud-save protocol guesswork.

## Deliberately not changed

- Official 2.11 Boomlings account login.
- GDPS backup/load response semantics.
- Flat shared local-save policy.
- 2.11 highest-graphics behavior.
