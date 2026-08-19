# Geometry Dash Wrapper 0.9.6-gdpstweaks1

Branch: `gdpstweaks1`

## Changes

- Removed the abandoned 1.3 hybrid ARM experiment completely.
  - The launcher no longer treats `lib/armeabi-v7a/libgame.so` as a legacy ARM package.
  - The legacy backend no longer contains the ARMv7 legacy-package mode, auxiliary-module mapper, hybrid constructor/JNI path, or hybrid-only symbol handling.
  - Normal legacy `lib/armeabi/libgame.so` support and the separate 2.2 ARMv7 backend remain intact.
- Reversed desktop editor movement sizing on x86, legacy ARM, and ARMv7:
  - Shift + W/A/S/D = small step.
  - W/A/S/D without Shift = big step.
- Added desktop window resizing with aspect-ratio-preserving letterboxing.
  - Mouse/touch coordinates are mapped through the displayed content rectangle.
  - OpenGL viewport/scissor calls are scaled to the resized client area.
- Added fullscreen toggle on all Windows backends:
  - F11
  - Alt+Enter
- Extras remains disabled for now.
