# Geometry Dash Wrapper 0.9.6-gdpstweaks13

Cross-version Windows wrapper for Geometry Dash Android builds.

## gdpstweaks13 regression-repair branch

This branch is a narrow correction to `gdpstweaks12`, based on the 2026-08-24 runtime logs. It deliberately leaves the confirmed 1.0 audio fix untouched.

### x86 editor hotkeys

The tweaks12 GameManager hard gate is removed. The 1.5/2.11-era x86 builds do not reliably expose their active `LevelEditorLayer` through the GameManager block, which is why every A/D/W/S/Q/E press was rejected in the editor.

Hotkey discovery is now tied to the actual Cocos running scene. A positive **or negative** EditorUI lookup is cached for that scene, so level-select/menu screens do not rescan thousands of nodes on every key press. A hidden EditorUI still blocks editor shortcuts during editor Play.

### ARMv7 editor Play crash

The 2022/2023 Play crash was an uninitialized native `ccArray` inside an otherwise valid-looking `CCArray` object. The wrapper now validates the internal storage before reusing an inherited collection and rebuilds only invalid editor arrays.

The internal `CCArray` layout is profile-correct: stock 2019 uses object `+0x20`, while the 2022/2023 Cocos builds use object `+0x30`. The stock Play paths themselves were audited: 2019 uses editor field `+0x2A4`, 2022 uses `+0x350`, and 2023 uses `+0x354`. The missing 2019 Play array is now part of the restored collection set as well.

### Preview Mode

Game variable `0036` is still observed, but both ON and OFF transitions now receive a short native background-update grace period. The previous implementation could suppress the first OFF transition update immediately. Preview animation and particle refresh callbacks are also requested on the next editor frame, and the host opacity bridge no longer forces ordinary editor dimming while Preview Mode is enabled.

### Preserved fixes

The APK-derived background/ground limits, dual GameManager gameplay/editor slots, 2019-vs-late editor input ABI split, null texture guards, and the internal Windows audio-volume behavior from tweaks11/12 remain in place.

### Scope

No APK, modded APK, `libgame.so`, or game asset is included. Missing textures that are genuinely absent from an APK remain an APK-content issue; this branch prevents the wrapper from treating nonexistent selector indices as valid.

See `GDPSTWEAKS13.md` and `GDPSTWEAKS13-VERIFICATION.txt` for the exact audit and remaining runtime-test boundary. Historical `GDPSTWEAKS*.md` files are retained as branch history.
