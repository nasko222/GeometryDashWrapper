# Geometry Dash Wrapper 0.9.6-gdpstweaks11

Cross-version Windows wrapper for Geometry Dash Android builds.

## gdpstweaks11 regression-fix branch

This branch closes the regressions reported against `0.9.6-gdpstweaks10` in the 2017 x86 and 2019/2022/2023 ARMv7 beta families.

### ARMv7 editor Play input

The editor Play input path is now profile-correct instead of pretending every beta has the same ABI:

- 2019 uses the stock `GJBaseGameLayer::pushButton(int,bool)` / `releaseButton(int,bool)` path with the editor-player flag set.
- 2022/2023 use stock `GJBaseGameLayer::queueButton(int,bool,bool)` with the final editor-player boolean set to `true`.
- The wrapper-restored GameManager active-layer field is corrected to `+0x158` for 2019 and `+0x168` for 2022/2023.

This targets the non-moving cube/mode in editor Play and the late-beta null active-layer crash path.

### Background/ground selector safety

Art limits are no longer hard-coded. The wrapper reads the APK central directory, determines the contiguous packaged ground/background ranges, and lowers the beta's own load/get clamps to the last texture actually present. A fixed APK with additional recreated contiguous textures is therefore allowed to expose those textures automatically.

Observed stock/reduced APK inventories used for this audit:

- 2019: 17 grounds, 20 backgrounds
- 2022: 18 grounds, 21 backgrounds
- 2023/SubZero: 18 grounds, 21 backgrounds

This restores the intended out-of-range behavior: resolve to the final packaged asset instead of continuing into a null-texture/freeze path.

### Preview Mode

The wrapper now observes GameManager variable `0036` (Preview Mode). Editor-only background freezing is disabled while Preview Mode is enabled or while editor Play is active, so the game's own background update path can run. The wrapper no longer requires entering Play once before the blue Preview Mode background appears.

### Windows audio volume isolation

The shared Windows effects backend no longer calls `waveOutSetVolume`. Effect/master gain is applied to PCM data before `waveOutWrite`, keeping wrapper SFX volume internal instead of moving the Windows mixer/session control. This shared backend is used by the x86, legacy ARM and ARMv7 wrappers.

Music keeps its existing private MCI stream-volume path; no endpoint/master Windows mixer setter is used by the wrapper.

### 2017 x86 editor hotkeys

A/D/W/S/Q/E no longer rebuild the full Cocos scene tree for every key press. `EditorUI` is cached while the scene is unchanged and validated directly. Hidden `EditorUI` is treated as editor Play/gameplay ownership, so edit shortcuts are not dispatched while the editor UI is hidden for Play.

### Scope

No APK, modded APK, `libgame.so`, or game asset is included. Missing textures that are genuinely absent from an APK remain an APK-content issue; this branch prevents the wrapper from treating nonexistent selector indices as valid.

See `GDPSTWEAKS11.md` and `GDPSTWEAKS11-VERIFICATION.txt` for the exact audit and remaining runtime-test boundary. Historical `GDPSTWEAKS*.md` files are retained as branch history.
