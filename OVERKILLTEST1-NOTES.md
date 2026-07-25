# OverkillTest1 diagnostic notes

## Goal

PerformanceTest1 proved that indexed APK extraction and direct OpenGL uploads
work, but the heavy Clutterfunk section can still take roughly 35 ms per
`nativeRender` while steady file reads and zlib work are zero. OverkillTest1
removes entire layers so the next optimization can target measured code rather
than another guess.

## What is genuinely removed

### Audio

`--no-audio` does not mute an initialized player. It prevents the Windows audio
bridge, MCI effect slots, WASAPI metering and APK audio cache from being
initialized. JNI audio methods return safe fake results without extracting,
decoding or playing files.

### PNG textures

When `--solid-textures`, `--no-textures` or `--headless-gl` is active, the
indexed ZIP accelerator substitutes every requested `.png` member with a valid
70-byte 1x1 white PNG. The original compressed image is not extracted and the
guest does not decode the original texture.

- `--solid-textures` uploads only a 1x1 white pixel.
- `--no-textures` discards texture uploads completely.
- `--headless-gl` never calls the host OpenGL driver.

The APK is still loaded once for `libgame.so` and its central-directory index.
That startup read is intentionally retained because the heavy steady-state log
already shows zero APK/file reads during the slowdown. Removing the APK entirely
would not isolate that steady bottleneck.

### Particles and cosmetics

The following ARM functions are hot-patched with tiny Thumb return stubs, so
there is no Unicorn callback per call:

- `CCParticleSystem` update/add/isActive/count/updateWithNoTime
- `CCParticleSystemQuad` draw/postStep
- `CCParticleBatchNode` draw/visit
- `CCMotionStreak` update/draw
- `GhostTrailEffect` snapshot/draw
- player streak, circle wave, light strip, ground and particle-scaling effects

### ARM drawing methods

F4 or `--no-node-draws` removes the ARM implementations of:

- `CCSprite::draw`
- `CCSpriteBatchNode::draw`
- `CCAtlasNode::draw`
- `CCLayerColor::draw`
- `CCProgressTimer::draw`
- `CCTextFieldTTF::draw`
- `CCLightning::draw`

Scene traversal and game update still run, but the common ARM-side visual
preparation functions return immediately.

## Isolation sequence

Use `RUN_ARM_NATIVE_BOOT.cmd`, reach the heavy section, and compare full
five-second profile windows.

| Test | Toggle | What a large FPS improvement means |
|---|---:|---|
| Visual overkill baseline | none | Audio, particles and original PNGs were not the core cause if lag remains. |
| Remove ARM draw methods | F4 | Sprite/label/shape preparation inside ARM is expensive. |
| Remove host draw calls | F6 | Driver draw submission or client-array preparation is expensive. |
| Headless OpenGL | F8 | Non-draw GL state, buffer and uniform bridge calls are expensive. |
| Remove scene traversal | F7 | Cocos node traversal, transforms and per-node draw dispatch dominate. |
| Remove `nativeRender` | F3 | Zero-render host-loop baseline. If this is fast but F7 is still slow, scheduler/update work inside `drawScene` remains. |

Toggle each option back off before the next test. F9 affects future texture
uploads and cannot recreate textures that were deliberately skipped.

## Strong launchers

- `RUN_OVERKILL_BARE_MINIMUM.cmd`: update/collision plus scene traversal, with
  audio, particles, ARM draw methods, textures and the host GL backend removed.
- `RUN_OVERKILL_LOGIC_ONLY.cmd`: also removes `CCNode::visit`.
- `RUN_OVERKILL_ZERO_RENDER.cmd`: never calls `nativeRender`; this measures the
  Windows host loop and swap baseline.
- `RUN_CONTROL_PERFORMANCETEST1.cmd`: unchanged control run.

The blank-screen launchers are mainly useful for menu-idle measurements. The
F3-F8 hotkeys are better for testing the exact same position inside a level.

## New log fields

`ARM overkill-test profile` reports:

- `audio-skips`
- `skipped-draws`
- `headless-gl`
- `texture-skips`
- `native-render-skips`
- flags `P,A,D,S,N,H,T` for particles, ARM draws, host draws, scene visit,
  nativeRender, headless GL and texture uploads.

## Validation performed

- C syntax validation of the complete wrapper source.
- Python build-script compilation checks.
- All particle, scene and ARM draw patch symbols verified in the bundled
  `libgame.so` symbol table.
- The 1x1 replacement byte array decoded as a valid RGBA PNG.
- Source-update patch reapplied to a clean PerformanceTest1 tree and compared
  against the packaged source.

A Windows runtime FPS result cannot be claimed until this build is run on the
actual wrapper and game APK.
