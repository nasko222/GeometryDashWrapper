# Geometry Dash Wrapper 0.9.6-gdpstweaks14

Tweaks14 is a narrow regression repair based directly on the 2026-08-24 tweaks13 runtime logs.

- ARMv7 2.2 editor: fixes the false CCArray validation failure that caused the wrapper itself to abort editor creation.
- x86 editor controls: restores the last user-confirmed working discovery path and caches non-editor misses per scene to avoid repeated menu spikes.
- Preview Mode: unchanged in this pass; retest only after the ARM editor can stay open.
- 1.0 music/audio: unchanged.

See `GDPSTWEAKS14.md` and `GDPSTWEAKS14-VERIFICATION.txt`.
