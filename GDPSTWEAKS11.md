# GDPSTWEAKS11

`0.9.6-gdpstweaks11` is a regression-fix pass based on the 2026-08-23 diagnostics from the 2017 x86 beta and the 2019/2022/2023 ARMv7 2.2-era beta families.

## Fixed paths

### 1. Editor Play input / non-moving player

The late-beta bridge previously called `queueButton` with the normal-game player selector while editor Play requires the editor-player boolean. The 2019 binary has no `queueButton` at all and therefore needs its native `pushButton` / `releaseButton` pair.

Tweaks11 selects the ABI by the already-audited stock editor profile and lets gameplay/playtest input win over desktop edit shortcuts.

### 2. GameManager active-layer regression

The host-restored editor wrote the active GJBaseGameLayer pointer four bytes too late. Tweaks11 uses the stock offsets verified from the corresponding UILayer paths:

- early 2019: `GameManager + 0x158`
- 2022/2023: `GameManager + 0x168`

The same profile-aware offset is used by the gameplay/editor bridge resolver.

### 3. Invalid art-selector freeze / null texture

The old protection was hard-coded to a donor APK's 18-ground/26-background assumption. Tweaks11 inventories the actual APK and lowers `GameManager::loadGround`, `getGTexture`, `loadBackground`, and `getBGTexture` to the contiguous packaged maximum. It validates the exact Thumb compare/immediate-move pattern before modifying it and supports both the 2019 and 2022/2023 instruction layouts.

### 4. Preview Mode state

All `GameManager::setGameVariable(char const*, bool)` callsites are routed through an observe-then-call thunk. The host tracks variable `0036` and otherwise leaves the original game function untouched. The editor-only camera/background suppression predicate now applies only when Preview Mode is off and editor Play is inactive.

### 5. Shared Windows mixer isolation

`waveOutSetVolume` is removed from the shared effects path. Original decoded PCM is retained per slot and software gain is applied before each `waveOutWrite`. No in-flight buffer is rewritten while waveOut owns it.

### 6. 2017 x86 hotkey spike

The editor hotkey path no longer recursively walks the complete Cocos scene graph on every key. It reuses a validated cached `EditorUI` while the running scene is unchanged. When the native node is hidden for editor Play, edit hotkeys are not consumed as editor transforms.

## Intentionally not claimed

This is a source/static/binary-audit build produced outside the user's Windows runtime. The exact fixes above are backed by the supplied APK binaries and diagnostics, but final runtime behavior must still be confirmed by running the rebuilt Windows executables. No unrelated crash is papered over with broad exception suppression or guessed offsets.
