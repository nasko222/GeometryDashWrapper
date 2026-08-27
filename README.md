# Geometry Dash Wrapper 0.9.6-gdpstweaks16

Tweaks16 is a focused ARMv7 2.2-beta editor repair based on the 2026-08-27 runtime logs and on the older EnduranceTest editor path where the song-position line and BPM guidelines were observed running.

- **2019 stock beta:** the wrapper now constructs/revalidates the exact `LevelEditorLayer::draw()` CCArray fields used at `+0x444` and `+0x298`, in addition to the `onPlaytest()` array at `+0x2A4`. The uploaded tweak15 run reached `EDITOR_INIT_OK` but crashed on the first editor draw because `+0x444` was null.
- **2022/2023 stock betas:** the exact Play scratch arrays (`+0x350` / `+0x354`) are revalidated after full editor construction, before editor touches, and before rendered editor frames. This targets arrays that can be replaced or degraded after the wrapper's first construction pass.
- **Song line / BPM guidelines:** the existing known-good per-frame overlay updater now uses the exact per-profile DrawGrid field first (`2019 +0x4E8`, `2022 +0x2C54`, `2023 +0x2C88`) and caches the DrawGrid object at creation. `levelSettingsUpdated()` remains the session-once native setup path for time markers.
- **Preview Mode:** variable `0036` was already being detected. When Preview is already enabled at editor entry, the stock preview animation/particle callbacks are reapplied after `levelSettingsUpdated()` has restored the editor settings/grid state.
- **x86 editor controls:** unchanged from tweaks15/tweaks14, where the user confirmed the key/menu issue fixed.
- **1.0 audio/music:** unchanged from tweaks15; the user-confirmed internal-volume fix is preserved.

This branch is source/static and exact-binary audited here, but it has not been executed as a Windows wrapper in this environment. The 2019 first-render and 2023 Play paths therefore remain runtime test targets rather than claimed confirmed fixes.

See `GDPSTWEAKS16.md` and `GDPSTWEAKS16-VERIFICATION.txt`.
