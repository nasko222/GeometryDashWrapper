# Geometry Dash Wrapper 0.9.6-gdpstweaks6

`gdpstweaks6` is a focused correction over tweaks5. It keeps wrapper-owned
editor restoration for stock ARMv7 2.2 betas and fixes the first Windows-test
failures found in the 2019 and 2022 profiles. It also expands FULL_BYPASS to the
late SubZero-style CreatorLayer layout and makes pause-button removal reassert
when the game makes the same Cocos node visible again.

## Stock 2.2 beta editor restoration

Recognized stock primary-library layouts:

- 2019 / early beta: `libcocos2dcpp.so` = 9,144,004 bytes.
- 2022 / Lite 2.2.11-era beta: 9,541,500 bytes.
- 2023 / SubZero 2.2.12-era beta: 9,578,364 bytes.

For recognized layouts no modded APK or `libgame.so` is required.

### tweaks6 corrections

- **2019:** the level setup string is now read from the exact stock
  `GJGameLevel + 0x110` field proven by `PlayLayer::init`, instead of using the
  old largest-string heuristic. If decompression fails, compressed garbage is
  never passed to `createObjectsFromSetup`.
- **2022:** `EditLevelLayer::init` is now read with its real level pointer at
  `EditLevelLayer + 0x14C`. tweaks5 incorrectly used the 2023 `+0x150` member.
- **2023:** keeps the separately verified `+0x150` EditLevelLayer member and
  existing late editor restoration profile.

The 2022/2023 stubbed `updateVisibility` path remains redirected to the host
visibility bridge. Unknown layouts fail closed instead of receiving guessed
stock-layout writes.

## FULL_BYPASS Creator Layer

The late CreatorLayer lock pattern is no longer hard-coded to `cmp.w r10,#0`.
The wrapper recognizes the same verified lock/tint block regardless of which
register carries the full-only flag. This covers the supplied 2022 build (r10)
and supplied 2023/SubZero build (r9), preserving the real button callbacks and
skipping the `onOnlyFullVersion`/140-opacity replacement block.

## Desktop gameplay options

Windows DPI scaling remains application-managed automatically on all wrapper
processes. Both optional gameplay UI settings remain disabled by default:

```bat
set "REMOVE_PAUSE_BUTTON=false"
set "HIDE_CURSOR_WHEN_PLAYING=false"
```

With `REMOVE_PAUSE_BUTTON=true`, ARMv7 now checks the actual Cocos visibility
state every frame instead of assuming that hiding a pause-item pointer once is
permanent. If the beta re-shows the same item during setup/restart, the wrapper
hides it again. Escape pause remains untouched.

## Confirmed carried behavior

- Early Android color picker compatibility repair.
- Large ARM/GDPS uploads are not truncated at 4095 formatted bytes.
- Legacy ARM cooperative/nonblocking networking and MCI music fixes.
- Resizable aspect-correct windows and F11/Alt+Enter borderless fullscreen.
- Editor controls: W/A/S/D big step, Shift+W/A/S/D small step, Q/E rotate.
- Extras remains temporarily hard-disabled.

## Building

Run `BUILD_ALL.cmd` on Windows. The source archive contains no APK, extracted
proprietary game library, game executable, DLL, or iOS backend.
