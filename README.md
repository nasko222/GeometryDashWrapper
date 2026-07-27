# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup15

Bringup15 keeps Bringup14's stable level and editor path and fixes the input
and cross-beta failures found in logs 19 and the two additional beta runs.

- Mouse-left on the platformer playfield and Space/Up all use the native jump
  path. Native pause and platformer-button hit testing remains in control.
- A/D and Left/Right are routed through `UILayer`; editor playtests use
  `EditorUI`, so movement works there and native platformer buttons can show
  their pressed state.
- F2 opens My Levels through the original debug shortcut.
- The selected late beta retains its validated, narrow companion editor bridge.
  Early betas use their primary-library editor; unknown or missing companion
  helpers are skipped instead of aborting the entire APK.
- Empty level setup recovery now validates the `GJGameLevel` object, scans
  compatible layouts, and can identify official levels from their music path.
  If recovery data is unavailable, control returns to the native beta instead
  of terminating the wrapper.
- Full companion `ApplyHooks`, constructors, DPAD touch hooks, collision hooks,
  and gameplay hooks remain disabled.

The stable Dynarmic Test14-fix1 line for Geometry Dash 1.0–1.4 is untouched.

## Run

1. Put your selected 2.2 beta APK beside the executable as `game.apk`.
2. Run `RUN_V22_SELECTED_APK.cmd`.

No APK or extracted game library is included.

## Build

On 64-bit Windows:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

Output: `dist-arm-wrapper-v22beta-bringup15\`

## Controls

- Jump: mouse, Space, or Up Arrow
- Platformer left: A or Left Arrow
- Platformer right: D or Right Arrow
- My Levels debug shortcut: F2

## Focused test

1. Replay one normal level in the selected newer beta.
2. In a platformer level, test mouse-left and Up for jump, then A/D and arrows.
3. Confirm keyboard movement visually presses the native left/right controls.
4. Open a platformer editor playtest and verify A/D and arrows move the player.
5. Press F2 in a menu and confirm My Levels opens.
6. With the early 2019 beta, open an official level and the editor.

Attach `gd-v22beta-selected.log` if a step fails.
