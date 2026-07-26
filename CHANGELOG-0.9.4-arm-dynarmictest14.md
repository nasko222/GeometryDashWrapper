# 0.9.4-arm-dynarmictest14

## Direct effect hook

`SimpleAudioEngine::playEffect` and `preloadEffect` are patched to host imports before execution. The play hook preserves R1-R3 and reads the soft-float pan/gain words from the original guest stack. This bypasses the old JniHelper/JNI path on every sound effect.

## Asynchronous effects

Effect commands are copied into a 256-entry lock-protected queue and executed by a dedicated above-normal-priority Windows worker. Decoder-slot lookup, MCI status/open/seek/volume/play, pause/resume/stop, unload, and master-volume application no longer block the ARM render call in normal operation.

Music remains on the established MCI path because the reported hitch affects short effects, not background music.

## Text-input prewarm

`chatFont-hd.fnt`, `chatFont-hd.png`, `loadingCircle-hd.png`, and `square02b_001-hd.png` are loaded into the native APK member cache before guest startup. Guest-side font construction can still have a one-time cost, but disk/member extraction is removed from that first interaction.

## Retained

All Test13 performance, profiling, networking, browser, save, editor-input, clean-exit, APK-cache, and diagnostics behavior is retained.
