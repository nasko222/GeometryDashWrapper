# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup14

Bringup14 keeps Bringup13's proven normal-level path and fixes the two failures
isolated by logs 17 and 18.

- Editor visibility is handled by a bounded native section scan. The old
  companion helper's 10,000-section ARM loop is not executed.
- Platformer control opacity is applied directly. The unrelated companion
  `GDPSManager::detectEmulators` path is not executed.
- Normal levels, large level strings, platformer keyboard input, music, and
  `waveOut` sound effects retain the Bringup13 implementation.
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

Output: `dist-arm-wrapper-v22beta-bringup14\`

## Controls

- Jump: mouse, Space, or Up Arrow
- Platformer left: A or Left Arrow
- Platformer right: D or Right Arrow

## Focused test

1. Replay the normal levels that were stable in Bringup13.
2. Open the editor, place several objects, move around, playtest, and return.
3. Load the same test platformer level from log 18.
4. Confirm both on-screen controls are visible and A/D/Space work.
5. Retry Power Trip, Knock Em Out, and Press Start.

Attach `gd-v22beta-selected.log` if a step fails.
