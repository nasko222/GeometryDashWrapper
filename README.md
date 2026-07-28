# Geometry Dash Wrapper 0.9.5-unified7-fix2-focused

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

## Unified7 Fix2 focused changes

- ARMv7 starts only the companion `ShaderFix` hook to restore black-mask shader
  objects that were rendering solid white.
- Editor viewport/scissor calls are no longer forcibly expanded, restoring
  narrow overlays such as the song-position line. The first editor color clear
  is promoted to a full default-framebuffer clear to remove the moving edge
  residue.
- Lite APKs missing all GauntletSheet resources receive a safe no-op gauntlet
  callback instead of crashing before a network request.
- The exact Geometry Dash 1.0.0 ARM library skips the forced-HD hook that caused
  its `nativeInit` failure. Other old ARM versions keep the configured setting.
- x86 Boomlings/configured-GDPS port-80 API traffic reaches the existing WinHTTP bridge
  without waiting for a guest nonblocking connect, and old 2.11 receives account
  server URLs as HTTP so it does not enter its crashing guest OpenSSL path.
- The real SubZero icon fills the Windows icon canvas better. Meltdown icon
  extraction also searches APK icon families outside `res/`.
- 2.11 highest-graphics behavior and comments are unchanged.

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
- Earlier Unified7 recovery attempted in-frame viewport/scissor normalization;
  Fix2 replaces that workaround with exact pass-through plus a targeted first
  color-clear repair.
- Includes real multi-resolution app icons extracted from game APK resources for
  Geometry Dash, Lite, World and SubZero. Meltdown uses its APK's real icon
  family; `icon.ico` or `icon.png` beside the launcher remains an explicit
  override.
- Window titles include Geometry Dash Lite, World, Meltdown and SubZero.

No APK or native Android `.so` file is included in the source archive.
