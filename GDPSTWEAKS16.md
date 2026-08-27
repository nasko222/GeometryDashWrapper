# GDPSTWEAKS16

Version: `0.9.6-gdpstweaks16`

Tweaks15 was still not usable for the restored 2.2 editors. The 2026-08-27 logs make the remaining failures considerably more specific, so tweaks16 deliberately avoids broad editor rewrites.

## 1. 2019: editor initialization succeeds, first draw crashes

The tweak15 2019 run reaches both `DYNARMIC_V22_WRAPPER_EDITOR_INIT_OK` and `DYNARMIC_V22_LEVEL_EDITOR_ENTERED`. The first editor render then faults in `LevelEditorLayer::draw()`.

Exact 9,144,004-byte binary disassembly shows the first failing access is:

- `LevelEditorLayer + 0x444` -> `CCArray::count()`

The same draw function also treats `LevelEditorLayer + 0x298` as a `CCArray` for group-overlay drawing. Tweaks15 restored neither field.

Tweaks16 adds both arrays to the Early2019 restoration set and treats these as critical collections together with the already-audited Play scratch array at `+0x2A4`.

## 2. 2022/2023: Play collection can become invalid after initial construction

Exact stock binaries use one Play scratch `CCArray`:

- 2019: editor `+0x2A4`
- 2022: editor `+0x350`
- 2023: editor `+0x354`

The 2023 tweak15 run reaches a functioning editor but still dies when Play enters `ccArrayDoubleCapacity()` and ultimately `realloc(nullptr, 0)`.

The wrapper previously validated these arrays only during an early construction pass. Full editor setup can subsequently replace inherited fields. Tweaks16 therefore checks only the exact crash-critical collections at three boundaries:

1. after full wrapper editor initialization (`post-init`);
2. immediately before editor touch callbacks (`pre-touch`), so the Play button cannot invoke `onPlaytest()` first;
3. immediately before each non-Play editor render (`pre-render`).

Healthy arrays are not replaced. Invalid/null/zero-capacity shells are rebuilt through the game's own `CCArray::create()` path.

The CCArray layouts remain profile-specific and binary-audited:

- 2019: `CCArray::_data` object `+0x20`, native element pointer `ccArray +0x08`;
- 2022/2023: `CCArray::_data` object `+0x30`, native element pointer `ccArray +0x0C`.

## 3. Song-position line and BPM guidelines restored to the old working state path

The current source still contained the EnduranceTest-era overlay updater, but current restored editors were not reaching it because `FindV22DrawGridLayer()` depended on a broad vtable scan.

The historical working run logged:

- `DYNARMIC_V22_DRAW_GRID_FOUND`
- `DYNARMIC_V22_EDITOR_SONG_GUIDE_FRAME` starting at frame 1 and continuing periodically
- `DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH` at frame 2

Tweaks16 restores deterministic DrawGrid ownership:

- the pointer returned by `DrawGridLayer::create()` is cached immediately;
- lookup first reads the exact profile field:
  - 2019 `+0x4E8`
  - 2022 `+0x2C54`
  - 2023 `+0x2C88`
- the old vtable scan remains only as a compatibility fallback for non-stock layouts.

The song guide continues using the native `DrawGridLayer::updateMusicGuideTime(float)` every editor frame. BPM/time-marker setup continues using the game's native `LevelEditorLayer::levelSettingsUpdated()` once per editor session after the first complete frame.

Exact `levelSettingsUpdated()` disassembly confirms the restored fields it needs:

- 2019: level `+0x4EC`, grid `+0x4E8`, LevelSettings `+0x28C`
- 2022: level `+0x138`, grid `+0x2C54`, LevelSettings `+0x338`
- 2023: level `+0x13C`, grid `+0x2C88`, LevelSettings `+0x33C`

All are fields already reconstructed by the wrapper.

## 4. Preview Mode

The new 2023 log proves Preview Mode variable `0036` is detected: the wrapper reports Preview enabled and runs editor visibility with full gameplay opacity. The flag observer itself is therefore not the missing piece.

Tweaks16 does not add another renderer replacement. Instead, if Preview is already enabled when the editor opens, the native `updatePreviewAnim()` and `updatePreviewParticles()` callbacks are reapplied once immediately after the session's native `levelSettingsUpdated()` call. This ensures Preview is refreshed after the DrawGrid/LevelSettings state that had regressed is available. Existing toggle handling and native-background grace behavior remain in place.

## 5. Frozen regressions

The following are intentionally byte-for-byte unchanged from tweaks15:

- `src/backends/x86/main.c`
- `src/shared/audio_win.c`

This preserves the user-confirmed x86 editor-key fix and 1.0 internal Windows-volume behavior.

## Runtime boundary

No Windows/Dynarmic build toolchain is available in this environment. Tweaks16 is exact-binary/source audited and patch-reconstruction tested, but runtime confirmation is still required for:

- 2019: editor survives first rendered frame;
- 2023: Play / Stop / Play survives;
- song-position line appears and moves;
- BPM guidelines appear on first editor open and after reopen;
- Preview Mode visually updates without first requiring Play.
