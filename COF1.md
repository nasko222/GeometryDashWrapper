# COF1 — Cleanup Optimization Fixes 1

Version: `0.9.7-cof1`  
Branch: `cof1`

## Why this branch exists

The `gdpstweaks5` through `gdpstweaks16` line progressively attempted to make
stock/reduced 2019, 2022 and 2023 2.2 beta APK editor stubs usable by rebuilding
large parts of `LevelEditorLayer` inside the Windows host. Runtime testing showed
that this became increasingly fragile: editor-load crashes, Play crashes,
missing Preview/BPM/song-guide state, invalid collections and finally visibly
incorrect gameplay/editor state.

COF1 removes that design from the active wrapper.

## Restored 2023 path

The ARMv7 editor path is returned to the EnduranceTest10/Bringup capability
model. If the APK contains the compatible late-beta companion initializer, the
wrapper creates the normal `LevelEditorLayer` object and calls only:

```text
LevelEditorLayerExt::initH(GJGameLevel*)
```

It does not run all companion constructors and does not run global `ApplyHooks`.
This is the same narrow policy used by the old stable selected-beta handler.

The following functions in COF1 are source-identical to EnduranceTest10:

- `ResolveV22InputBridgeSymbols`
- `PatchV22ArtAssetLimits`
- `InstallV22EditButtonBridge`
- `InstallV22SafeVisualHooks`
- `FindV22DrawGridLayer`
- `UpdateV22EditorOverlayFrame`
- `HostV22UpdateCameraBackground`
- `HostV22EditorVisibility`
- `HostV22BatchUpdateBlend`
- `EnterV22LevelEditor`
- `SendPlatformerButton`
- `PrepareV22MousePlatformerTouch`
- `SyncV22MousePlatformerJump`

That restores the old song-position guide/BPM update path and the old editor
visibility/background behavior instead of layering more fixes over the broken
stock reconstruction.

## Selected modded 2023 APK compatibility audit

The selected APK used for verification has SHA-256:

```text
b0b4c1dfc63040531c856a178e15dcc28312511ece245766844b59fcbe326fb1
```

Its ARMv7 primary library is 9,578,364 bytes and its packaged `libgame.so`
exports a 2,044-byte `LevelEditorLayerExt::initH`. Direct Thumb disassembly
shows the companion writing the expected late-beta editor fields at `+0x13C`
and `+0x354`. Those are the exact compatibility checks used by the old handler.
The APK and its proprietary libraries are verification inputs only and are not
included in COF1 packages.

## Stock 2019/2022 policy

COF1 intentionally does not promise a working editor for APKs whose primary
`LevelEditorLayer::init` is only the stock stub and which do not supply a
compatible editor companion. Those APKs should be fixed later on the APK side,
not by reconstructing C++ game objects in the host wrapper.

## Preserved confirmed fixes

- x86 backend source is byte-for-byte unchanged from `gdpstweaks16`.
- legacy ARM behavior is unchanged apart from the branch/version diagnostic
  string.
- `src/shared/audio_win.c` is byte-for-byte unchanged, preserving the confirmed
  Windows-mixer isolation fix.
