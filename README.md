# Geometry Dash Wrapper 0.9.5-unified4

Unified4 keeps the three proven execution cores and fixes the launch/network/
visual issues found while testing Unified2:

- **x86 native, first priority:** `0.9.3-alpha3`
- **legacy ARM/Thumb through Dynarmic:** `dynarmictest14-fix1`
- **ARMv7 / 2.2 through Dynarmic:** `0.9.4-milestone1`

There is no Unicorn backend or dependency. F2 remains removed. All backends use
one `dist-unified/save/` directory.

## Launch settings

Edit the values at the top of `RUN_AUTO.cmd` before running:

```bat
set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "FORCE_HIGHEST_GRAPHICS=true"
set "MUSIC_PULSE_MAX=0.30"
set "OVERRIDE_ARM=false"
```

- `GDPS_SERVER` routes levels, accounts, comments, gauntlets and other game API
  calls while preserving the complete relative endpoint path. For example,
  `/database/accounts/loginGJAccount.php` remains under `/accounts/`.
- `getGJSongInfo.php` first uses the configured GDPS, allowing private custom
  song catalogues. If that endpoint is missing, returns `-1`, or emits HTML/PHP
  proxy warnings, the wrapper retries official HTTPS Boomlings. The returned
  CDN/song URL itself is never rewritten.
- `HACK_ICONS=true` makes supported icon and color ownership checks return
  unlocked for that run without writing fake unlocks to the save.
- `FULL_BYPASS=true` applies only hooks that exist in the selected game. It
  redirects the spin-off Full Version button through the normal Creator path
  and enables exported online-level capability checks. It does **not** globally
  remap restricted buttons to My Levels, so each available button keeps its own
  native destination. Missing or differently compiled exports are skipped.
- `FORCE_HIGHEST_GRAPHICS=true` forces exported HD checks true and low-memory
  checks false. APKs without those exports keep their normal behavior.
- `MUSIC_PULSE_MAX` caps the DSP value used by music-reactive/rave visuals. It
  does not lower audio volume. Lower it (for example `0.20`) for weaker pulses.
- `OVERRIDE_ARM=false` keeps x86 first. `true` prefers ARMv7, then legacy ARM,
  when the APK contains multiple architectures.

## Unified4 fixes

- Keeps account/login routing unchanged from Unified3. The reported official
  login failure was a wrong password, not a wrapper failure.
- Normalizes a large persistent edge `glScissor` or `glViewport` issued on the
  default ARMv7 framebuffer. This targets the black strip that grows during an
  editor playtest and persists until the editor is reopened.
- Re-enables the late-beta swing selector whenever Level Settings is opened;
  switching to platformer mode can still hide/reset wave and swing normally.
- `HACK_ICONS=true` unlocks exported icon **and color** ownership checks.
- `FULL_BYPASS=true` no longer maps every restricted Creator button to My
  Levels. It enables the game capability checks and preserves each button's
  native callback; enabled appearance is therefore controlled by the game's
  normal CreatorLayer construction.
- `FORCE_HIGHEST_GRAPHICS=true` forces exported HD checks true and low-memory
  checks false where those symbols exist.
- The launcher uses `icon.png` beside `RUN_AUTO.cmd`/inside `dist-unified` when
  supplied; otherwise it chooses the highest suitable PNG launcher icon from
  the APK. The selected image is applied to the live Windows title bar/taskbar.
- Legacy ARM ZIP/minizip/browser/effect hooks are capability-based optional
  accelerators. A small prologue difference such as `B508` versus `B510` no
  longer aborts Geometry Dash 1.6; unmatched functions run their guest code.
- The x86 and legacy ARM socket bridges recognize complete Geometry Dash PHP
  requests and serve them through WinHTTP, including requests split across more
  than one guest send. This allows old plaintext-libcurl games such as 1.6 to
  reach HTTPS Boomlings/GDPS APIs on either selected old-game backend.
- `OVERRIDE_ARM=true` still selects ARM only when the APK actually contains a
  supported ARM payload. Native x86 remains the default priority otherwise.

## Important limits

- Runtime icon replacement changes the live window/taskbar icon. It does not
  rewrite the icon resource embedded inside the already-built EXE in Explorer.
- Creator-button unlocking is symbol/capability based. APKs that strip every
  relevant export may keep some spin-off restrictions.
- Highest graphics is best-effort; an APK without the exported platform checks
  keeps its own graphics selection.
