# Geometry Dash Wrapper 0.9.6-gdpstweaks15

Tweaks15 is an ARMv7-only 2.2-beta editor repair based on the 2026-08-24 tweaks14 runtime logs.

- 2023 editor Play: reject/rebuild zero-capacity CCArray shells before `onPlaytest()` can enter `ccArrayDoubleCapacity` with a null element buffer.
- 2019 editor button: accelerate the reduced-APK EditorUI construction by pre-indexing packaged sprite-frame names and bypassing stock dictionary lookups only for frames proven absent from that APK; suppress thousands of synchronous per-frame log flushes.
- x86 editor controls: unchanged from tweaks14 (user-confirmed fixed).
- Preview Mode: unchanged in this pass.
- 1.0 music/audio: unchanged.

See `GDPSTWEAKS15.md` and `GDPSTWEAKS15-VERIFICATION.txt`.
