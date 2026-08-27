# Geometry Dash Wrapper 0.9.7-cof1

`cof1` means **Cleanup Optimization Fixes 1**.

This branch deliberately ends the wrapper-owned stock 2.2 beta editor experiment.
The ARMv7 backend no longer tries to fabricate the missing editor runtime shipped
as four-byte stubs in the reduced 2019/2022/2023 APKs.

## ARMv7 2.2 policy

- The modded/selected 2023 beta uses the old Endurance/Bringup capability path:
  `LevelEditorLayer::create` -> validated `LevelEditorLayerExt::initH` from the
  APK's own compatible `libgame.so` -> normal Cocos scene.
- The EnduranceTest10 editor visibility, song-guide/BPM overlay, background,
  blend, platformer input and editor-entry handlers are restored exactly.
- No global `libgame.so` constructors or `ApplyHooks` pass is run. Only the
  validated editor capability is used.
- Stock 2019/2022/2023 editor stubs are intentionally **not repaired by the
  wrapper**. Their normal gameplay may still run, but editor support should be
  provided later by an APK-side mod/compatible companion rather than host-side
  object reconstruction.
- ARMv7 wrapper-owned W/A/S/D/Q/E editor movement/rotation injection is removed.
  Editor keyboard behavior belongs to the game/companion; A/D remain normal
  platformer controls.

## Preserved backends and fixes

The x86 backend is carried byte-for-byte from the accepted `gdpstweaks16`
source. The legacy ARM backend is also carried forward; only its displayed
version string changes to `0.9.7-cof1`.

The shared Windows audio implementation is byte-for-byte unchanged, preserving
the confirmed internal SFX/master-volume behavior that does not move the
Windows Volume Mixer.

Existing GDPS/network, save, launcher, DPI, graphics, FULL_BYPASS and other
non-editor functionality remains in the unified source unless specifically
noted in `COF1.md`.

## Build

On 64-bit Windows run:

```bat
BUILD_ALL.cmd
```

The complete unified output is written to `dist-unified\`.
Drag an APK onto `RUN_AUTO_GDPS.cmd` or `RUN_AUTO_BOOMLINGS.cmd`.

The source package contains no APK, no extracted proprietary `.so`, and no
prebuilt game binary.
