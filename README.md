# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup17

Bringup17 starts the `v22beta-bringup17-safe-companions` branch from the stable
Bringup16 runtime. It fixes the lost-local-save regression and adds targeted
editor entry from gameplay without enabling the beta's broad mod-hook stack.

## What changed

- Every Android `/data/data/<package>/...` save path now redirects into the
  portable `save-v22beta` folder. This covers the official package, SubZero,
  GDPS Editor, and renamed community beta packages instead of recognizing only
  `com.robtopx.geometryjump`.
- On first run, the Windows build scans the current/APK/save drive roots for
  saves accidentally written to `X:\data\data\<package>\` by Bringup16 and
  copies `CCGameManager.dat`, `CCLocalLevels.dat`, and their `.bak` files into
  `save-v22beta` when the local copy is missing.
- `PauseLayer::onEdit` and `EndLevelLayer::onEdit` are bridged to the same
  ABI-validated editor entry already used by the wrench/hammer button. This is
  targeted: no `libgame.so` constructors or complete `ApplyHooks` pass runs.
- Startup lists every packaged ARM `.so` and records the loading policy.
  `libcocos2dcpp.so` is primary, compatible `libgame.so` may be mapped for its
  validated editor initializer, and the remaining hook engines stay audit-only.
- Bringup16's platformer mouse/keyboard and editor-playtest input fixes remain.

## Why all `.so` files are not blindly loaded

The selected beta's `libgame.so` contains editor code, but also DPAD,
collision, shader, timer, GDPS, options, hacks, server, emoji, search, icon, and
debug hook sets. `libhooking.so`, `libdobby.so`, and old `libgdkit.so` are hook
engines, not independent game modules. Running every constructor and every
`ApplyHooks` routine would allow them to rewrite primary-library functions and
the wrapper's stable bridges in an APK-specific order.

Bringup17 therefore inventories all libraries but executes only known,
ABI-validated capabilities. The additional gameplay/mod hooks remain disabled
in the friend-test build.

## Editor support boundary

The selected late beta includes a compatible `LevelEditorLayerExt::initH`, so
wrench/hammer, pause-menu Edit, and end-level Edit use that targeted
initializer. Stock SubZero and the supplied early beta expose only the base
`LevelEditorLayer::init` stub and contain no compatible companion editor
implementation. F2 can still open My Levels there, but Bringup17 cannot safely
invent the missing editor runtime.

## Run

1. Put the chosen 2.2 beta APK beside the executable as
   `game-v22beta-selected.apk` (or build with its path).
2. Run `RUN_V22_SELECTED_APK.cmd`.
3. Existing portable data is under `save-v22beta`. Misplaced Bringup16 data is
   copied there automatically when possible. Before rebuilding,
   `RECOVER_V22_SAVES.cmd -ListOnly` shows the old drive-root files, and running
   it without `-ListOnly` copies the newest missing saves into `save-v22beta`.

No APK or extracted proprietary native library is included in the source
archive.

## Build

On 64-bit Windows:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

Output: `dist-arm-wrapper-v22beta-bringup17\`

## Focused friend test

1. Confirm existing icons/settings and local levels appear from
   `save-v22beta`.
2. Replay normal and platformer levels; test mouse, Space, Up, A/D, and arrows.
3. In the selected late beta, test wrench/hammer Edit, pause-menu Edit, and
   end-level Edit.
4. Press F2 from a menu and confirm My Levels opens.
5. Test stock SubZero only as a capability check: it should refuse unsafe full
   editor entry rather than freeze or crash.

Attach `gd-v22beta-selected.log` if a step fails.
