# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest14

DynarmicTest14 keeps Test13's large CPU-emulation speedup, working GDPS networking, browser links, and debug-everything profiler. It targets the remaining short stalls caused by sound effects and the first text-field/keyboard opening.

## Test14 changes

- Hooks `CocosDenshion::SimpleAudioEngine::playEffect` directly at the ARM entry point.
- Hooks `SimpleAudioEngine::preloadEffect` directly as well.
- Skips the old guest JNI/JniHelper effect path, eliminating its per-sound emulation overhead.
- Queues effect preload/play/volume/pause/resume/stop/unload commands to a dedicated above-normal-priority Windows worker.
- Keeps decoded WAV effects and MCI decoder slots cached; no synchronous MCI effect commands run on the render thread during normal operation.
- Preserves pitch, pan, gain, loop, and returned effect identifiers. Pitch and pan remain accepted but are not transformed by the existing MCI backend, matching prior wrapper behavior.
- Prewarms the chat-font and loading assets in the native APK-member cache to reduce the first text-input hitch.
- Retains Test13 guest page lookup, typed memory callbacks, cached OpenGL dispatch, profiler CSV/summary, internet, browser links, saves, and audio music behavior.

## Build

Run:

```bat
BUILD_DYNARMIC_X64.cmd
```

The output is:

```text
dist-arm-wrapper-dynarmictest14
```

Run `RUN_DYNARMIC_INTERACTIVE.cmd`. Useful markers include:

```text
RESULT: DYNARMIC_SIMPLEAUDIO_EFFECT_HOOKS_READY count=2 play=direct-host preload=direct-host async-worker=1
RESULT: DYNARMIC_ASYNC_EFFECT_WORKER_READY queue=256
RESULT: DYNARMIC_TEXT_INPUT_ASSET_PREWARM_READY count=4
```

For the audio test, die repeatedly in a level and verify that the sound still plays without a 100-300 ms frame. Also open a text field twice and compare the first and second opening.
