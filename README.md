# Geometry Dash Wrapper 0.9.5 — Unified7 Fix4 Native Stabilization

One wrapper source tree for:

- x86 Android Geometry Dash builds;
- legacy ARM/Thumb builds through Dynarmic;
- ARMv7/2.2 beta builds through Dynarmic.

## Running

The distributed wrapper no longer requires Python.

Use either launcher:

- `RUN_AUTO_GDPS.cmd` — `naskogdps17.7m.pl/database`
- `RUN_AUTO_BOOMLINGS.cmd` — `www.boomlings.com/database`

Double-click a launcher to use `game.apk`, or drag any APK onto it. The native
`GeometryDashLauncher.exe` detects the APK architecture, package and version,
selects the backend, applies the correct window title/icon, and creates a unique
folder under `logs\YYYY-MM-DD\...`.

All supported versions continue to use the single portable `save\` directory.
The launcher does not create per-version save profiles.

## Fix4 stabilization changes

- Replaces the runtime Python selector with a native Win32 C launcher.
- Removes the Python dependency from the x86 build script as well.
- Adds separate GDPS and Boomlings launch BAT files, both supporting APK drag-and-drop.
- Captures x86 logs directly in the dated run folder. Previous launcher builds
  missed x86 logs because that backend changed its working directory before
  opening `gd-wrapper.log`.
- ARMv7 companion hooks default to OFF, and the launcher no longer supplies the
  malformed/rejected option that prevented every supplied 2.2-beta test from booting.
- Keeps the direct desktop-GLSL compatibility path and restores guest ownership
  of editor viewport/scissor/clear state; the experimental companion ShaderFix is removed.
- Keeps the targeted desktop keyboard-offset suppression for x86 versions that
  export the Android keyboard-movement callbacks.
- Keeps the exact Geometry Dash 1.0 minizip/HD guards while leaving 1.01+ behavior unchanged.
- Bundles real multi-resolution icons for Dash, Lite, World, SubZero and Meltdown.
- Contains no `.gitignore` file.

The official 2.11 Boomlings login behavior is intentionally not spoofed because
it also fails in the Android emulator. A GDPS may still support that old client.

## Readability

Start with `docs/CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`. The new launcher is heavily
commented and deliberately organized around record-like structs and small helper
functions. Existing emulator backends were not cosmetically mass-refactored:
readability-only material is comments/documentation so it cannot alter behavior.
Actual bug fixes naturally produce different backend binaries.
