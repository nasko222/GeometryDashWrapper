# Geometry Dash Wrapper 0.9.5-unified7-recovery

Recovery build of the unified x86, legacy ARM and ARMv7/Dynarmic wrapper.
The experimental EXE/DLL/CFG launcher from Unified6 is removed. The normal
entry point is again `RUN_AUTO.cmd`, with the three backend EXEs stored under
`x86`, `arm-legacy` and `armv7`.

## Run settings

Edit `RUN_AUTO.cmd`:

```bat
set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "FORCE_HIGHEST_GRAPHICS=true"
set "MUSIC_PULSE_MAX=0.30"
```

Place the APK at `dist-unified\game.apk` and launch `RUN_AUTO.cmd`.
x86 is preferred when an x86 game library is present. The removed ARM override
setting does not return.

## Recovery changes

- Restores the old CMD launcher and backend EXE layout.
- Removes per-package save profiles. Every supported version uses the single
  portable `dist-unified\save` directory.
- Restores the pre-Unified6 CCHttpResponse byte-vector layout and the older
  callback budget/delivery behavior. Completed requests are delivered in request
  order during the same pump pass.
- Patches the actual Thumb block used by the attached Geometry Dash Lite build,
  skipping the callback replacement and grey tint that route Creator buttons to
  `onOnlyFullVersion`.
- During an active editor scene, non-full default-framebuffer `glViewport` and
  `glScissor` calls are normalized immediately inside the render pass. This is
  targeted at the scrolling right-side black region rather than applying another
  blind pre/post-frame reset.
- Includes real multi-resolution app icons extracted from game APK resources for
  Geometry Dash, Lite, World and SubZero. Meltdown uses its APK's real icon
  family; `icon.ico` or `icon.png` beside the launcher remains an explicit
  override.
- Window titles include Geometry Dash Lite, World, Meltdown and SubZero.

No APK or native Android `.so` file is included in the source archive.
