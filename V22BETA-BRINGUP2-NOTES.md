# Geometry Dash 2.2 beta ARMv7 bringup2

## Primary fix

Bringup1 reached the first authentic constructor, which used ARM exclusive-access instructions and triggered Dynarmic's `conf.global_monitor != nullptr` assertion. Bringup2 owns a one-processor `Dynarmic::ExclusiveMonitor` for the full lifetime of the JIT and also attaches one to the ARMv7 feature smoke test.

## Dual-beta support

The source includes separate launchers and logs for both supplied APKs:

- `game-v22beta.apk`: newer, 349 constructors.
- `game-v22beta1.apk`: earlier, 327 constructors and two extra FMOD stream-buffer APIs.

The FMOD bridge now stores and returns stream-buffer size/type rather than treating those output calls as empty success stubs.

## Shared first-use fixes

- Every bundled MP3 is materialized into `save-v22beta/audio-cache` before constructors/native initialization.
- Text-entry font/UI assets are preloaded when an APK is available.
- Raw `.so` probe mode skips APK-only cache work cleanly.

## Launchers after building

```text
RUN_V22_NEWER_APK.cmd
RUN_V22_EARLIER_APK.cmd
RUN_V22_RAW_SO_PROBE.cmd
```

This remains a bring-up branch. Reaching `nativeInit` or a window is not claimed until Windows logs confirm it.
